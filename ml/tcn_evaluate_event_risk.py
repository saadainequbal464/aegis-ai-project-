from pathlib import Path
import json
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
from sklearn.metrics import average_precision_score, roc_auc_score, confusion_matrix

RUNTIME_FEATURE_NAMES = [
    'cpu_mean','cpu_max','cpu_std','cpu_trend','memory_mean','memory_max','memory_trend',
    'gpu_mean','gpu_max','gpu_trend','gpu_memory_ratio_mean','gpu_memory_ratio_max','gpu_memory_ratio_trend','gpu_temperature_mean','gpu_temperature_max',
    'npu_mean','npu_max','npu_trend','dram_read_mean','dram_read_max','dram_write_mean','dram_write_max','dram_util_mean','dram_util_max','dram_util_trend',
    'frame_time_mean','frame_time_max','frame_time_std','frame_time_trend','fps_mean','fps_min','fps_trend','token_rate_mean','token_rate_min','token_rate_trend',
    'token_latency_mean','token_latency_max','token_latency_trend','ttft_mean','ttft_max'
]
HORIZONS=[250,750,2000]; THRESHOLD=1.01; SEQ=20
ROOT=Path(__file__).resolve().parent.parent
SPLIT=ROOT/'telemetry/data/processed/splits'
CKPT=ROOT/'models/tier2_tcn_event_risk/tcn_event_risk_best.pt'
DEVICE=torch.device('cuda' if torch.cuda.is_available() else 'cpu')

class Block(nn.Module):
    def __init__(self,ic,oc,k,d,drop=.1):
        super().__init__(); self.padding=(k-1)*d
        self.c1=nn.Conv1d(ic,oc,k,padding=self.padding,dilation=d); self.c2=nn.Conv1d(oc,oc,k,padding=self.padding,dilation=d)
        self.act=nn.ReLU(); self.drop=nn.Dropout(drop); self.res=nn.Conv1d(ic,oc,1) if ic!=oc else nn.Identity()
    def forward(self,x):
        r=self.res(x); x=self.c1(x); x=x[:,:,:-self.padding]; x=self.drop(self.act(x)); x=self.c2(x); x=x[:,:,:-self.padding]; x=self.drop(self.act(x)); return x+r
class TCN(nn.Module):
    def __init__(self):
        super().__init__(); self.net=nn.Sequential(Block(40,64,3,1),Block(64,64,3,2),Block(64,64,3,4),Block(64,32,3,8)); self.head=nn.Sequential(nn.Linear(32,32),nn.ReLU(),nn.Linear(32,3))
    def forward(self,x): return self.head(self.net(x.transpose(1,2))[:,:,-1])

def load(name):
    df=pd.read_csv(SPLIT/f'{name}.csv').sort_values(['session_id','timestamp_us']).reset_index(drop=True)
    X=df[RUNTIME_FEATURE_NAMES].astype(np.float32).to_numpy()
    rho=df[[f'rho_{h}ms' for h in HORIZONS]].apply(pd.to_numeric,errors='coerce').to_numpy()
    y=(rho>=THRESHOLD).astype(np.float32)
    seq=[]; tar=[]; meta=[]
    sid=df.session_id.to_numpy()
    start=0
    while start<len(df):
        end=start+1
        while end<len(df) and sid[end]==sid[start]: end+=1
        if end-start>=SEQ:
            for i in range(start+SEQ-1,end):
                seq.append(X[i-SEQ+1:i+1]); tar.append(y[i]); meta.append((int(sid[i]),int(df.timestamp_us.iloc[i])))
        start=end
    return np.asarray(seq,np.float32),np.asarray(tar,np.float32),meta

def main():
    print('Checkpoint:',CKPT)
    print('Exists:',CKPT.exists())
    if not CKPT.exists(): raise FileNotFoundError(CKPT)
    ck=torch.load(CKPT,map_location=DEVICE,weights_only=False)
    print('Checkpoint task:',ck.get('task')); print('Best checkpoint schema:',len(ck.get('feature_names',[])),'features',ck.get('horizons_ms')); print('Event threshold:',ck.get('event_threshold'))
    if ck.get('feature_names')!=RUNTIME_FEATURE_NAMES: raise ValueError('Checkpoint feature schema mismatch')
    model=TCN().to(DEVICE); model.load_state_dict(ck['model_state_dict']); model.eval()
    results={}
    for split in ['validation','test']:
        X,y,meta=load(split); probs=[]
        with torch.no_grad():
            for i in range(0,len(X),256): probs.append(torch.sigmoid(model(torch.from_numpy(X[i:i+256]).to(DEVICE))).cpu().numpy())
        p=np.concatenate(probs); results[split]={}
        print(f'\n=== {split.upper()} ===')
        print('sequences:',len(X),'positive counts:',y.sum(axis=0).astype(int).tolist())
        for j,h in enumerate(HORIZONS):
            yt=y[:,j].astype(int); pr=p[:,j]; pos=int(yt.sum()); neg=len(yt)-pos
            d={'positive_count':pos,'negative_count':neg,'prob_min':float(pr.min()),'prob_max':float(pr.max()),'prob_mean':float(pr.mean()),'prob_median':float(np.median(pr))}
            if pos and neg:
                pred=(pr>=.5).astype(int); tn,fp,fn,tp=confusion_matrix(yt,pred,labels=[0,1]).ravel()
                d.update(roc_auc=float(roc_auc_score(yt,pr)),pr_auc=float(average_precision_score(yt,pr)),tp=int(tp),fp=int(fp),tn=int(tn),fn=int(fn),recall_at_0_5=float(tp/max(tp+fn,1)),fpr_at_0_5=float(fp/max(fp+tn,1)))
            results[split][f'{h}ms']=d
            print(f'{h}ms:',json.dumps(d,sort_keys=True))
        if split=='test':
            for j,h in enumerate(HORIZONS):
                pr=p[:,j]; order=np.argsort(-pr)[:10]
                print(f'\nTop 10 {h}ms predicted-risk windows:')
                for k in order:
                    print(f'  p={pr[k]:.6f} session={meta[k][0]} timestamp_us={meta[k][1]} actual_event={int(y[k,j])}')
    out=ROOT/'models/tier2_tcn_event_risk/evaluation.json'; out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(results,indent=2)); print('\nSaved:',out)
if __name__=='__main__': main()
