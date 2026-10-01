import {
  Area,
  AreaChart,
  CartesianGrid,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from "recharts";

/* =========================================================
   SHARED TOOLTIP
========================================================= */

function ChartTooltip({ active, payload, label, formatter }) {
  if (!active || !payload?.length) return null;

  return (
    <div className="chart-tooltip">
      <div className="chart-tooltip-label">{label}</div>

      <div className="chart-tooltip-values">
        {payload.map((entry) => {
          const value = formatter
            ? formatter(entry.value, entry.name)
            : entry.value;

          return (
            <div className="chart-tooltip-row" key={entry.dataKey}>
              <span className="chart-tooltip-name">
                {entry.name}
              </span>

              <strong>{value}</strong>
            </div>
          );
        })}
      </div>
    </div>
  );
}

/* =========================================================
   CHART WRAPPER
========================================================= */

export function ChartContainer({
  children,
  height = 280,
  className = ""
}) {
  return (
    <div
      className={`chart-container ${className}`.trim()}
      style={{ height }}
    >
      <ResponsiveContainer width="100%" height="100%">
        {children}
      </ResponsiveContainer>
    </div>
  );
}

/* =========================================================
   RISK HISTORY CHART
========================================================= */

export function RiskHistoryChart({
  data = [],
  height = 300,
  showThreshold = true,
  threshold = 0.5,
  showGrid = true,
  yDomain = [0, 1]
}) {
  return (
    <ChartContainer height={height}>
      <AreaChart
        data={data}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        {showGrid && (
          <CartesianGrid
            stroke="rgba(255,255,255,0.055)"
            vertical={false}
          />
        )}

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          minTickGap={35}
        />

        <YAxis
          domain={yDomain}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(value) =>
            `${Math.round(value * 100)}%`
          }
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value) =>
                `${(Number(value) * 100).toFixed(1)}%`
              }
            />
          }
        />

        {showThreshold && (
          <ReferenceLine
            y={threshold}
            stroke="#e7b45c"
            strokeDasharray="4 4"
            strokeOpacity={0.65}
          />
        )}

        <defs>
          <linearGradient
            id="riskHistoryFill"
            x1="0"
            y1="0"
            x2="0"
            y2="1"
          >
            <stop
              offset="0%"
              stopColor="#7c9cff"
              stopOpacity={0.24}
            />

            <stop
              offset="100%"
              stopColor="#7c9cff"
              stopOpacity={0}
            />
          </linearGradient>
        </defs>

        <Area
          type="monotone"
          dataKey="risk"
          name="Risk"
          stroke="#7c9cff"
          strokeWidth={2}
          fill="url(#riskHistoryFill)"
          dot={false}
          activeDot={{
            r: 4,
            strokeWidth: 0
          }}
        />
      </AreaChart>
    </ChartContainer>
  );
}

/* =========================================================
   FORECAST CONE
========================================================= */

export function ForecastConeChart({
  data = [],
  height = 320,
  threshold = 0.5,
  showThreshold = true
}) {
  const processedData = data.map((item) => ({
    ...item,
    range:
      item.q90 !== undefined && item.q10 !== undefined
        ? Math.max(0, item.q90 - item.q10)
        : undefined
  }));

  return (
    <ChartContainer height={height}>
      <AreaChart
        data={processedData}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        <CartesianGrid
          stroke="rgba(255,255,255,0.055)"
          vertical={false}
        />

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          minTickGap={30}
        />

        <YAxis
          domain={[0, 1]}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(value) =>
            `${Math.round(value * 100)}%`
          }
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value, name) => {
                if (value === undefined) return "—";

                return `${(Number(value) * 100).toFixed(1)}%`;
              }}
            />
          }
        />

        {showThreshold && (
          <ReferenceLine
            y={threshold}
            stroke="#e7b45c"
            strokeDasharray="4 4"
            strokeOpacity={0.7}
          />
        )}

        <defs>
          <linearGradient
            id="forecastBand"
            x1="0"
            y1="0"
            x2="0"
            y2="1"
          >
            <stop
              offset="0%"
              stopColor="#a78bfa"
              stopOpacity={0.18}
            />

            <stop
              offset="100%"
              stopColor="#a78bfa"
              stopOpacity={0.025}
            />
          </linearGradient>
        </defs>

        <Area
          type="monotone"
          dataKey="q90"
          name="q90"
          stroke="none"
          fill="url(#forecastBand)"
          stackId="forecast"
        />

        <Area
          type="monotone"
          dataKey="q10"
          name="q10"
          stroke="none"
          fill="#090b0f"
          fillOpacity={0.75}
          stackId="forecast"
        />

        <Line
          type="monotone"
          dataKey="q50"
          name="Median"
          stroke="#a78bfa"
          strokeWidth={2.2}
          dot={false}
          activeDot={{
            r: 4,
            strokeWidth: 0
          }}
        />
      </AreaChart>
    </ChartContainer>
  );
}

/* =========================================================
   MULTI-HORIZON FORECAST
========================================================= */

export function HorizonForecastChart({
  data = [],
  height = 300
}) {
  return (
    <ChartContainer height={height}>
      <LineChart
        data={data}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        <CartesianGrid
          stroke="rgba(255,255,255,0.055)"
          vertical={false}
        />

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
        />

        <YAxis
          domain={[0, 1]}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(value) =>
            `${Math.round(value * 100)}%`
          }
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value) =>
                `${(Number(value) * 100).toFixed(1)}%`
              }
            />
          }
        />

        <Line
          type="monotone"
          dataKey="h250"
          name="250 ms"
          stroke="#7c9cff"
          strokeWidth={2}
          dot={false}
        />

        <Line
          type="monotone"
          dataKey="h750"
          name="750 ms"
          stroke="#a78bfa"
          strokeWidth={2}
          dot={false}
        />

        <Line
          type="monotone"
          dataKey="h2000"
          name="2000 ms"
          stroke="#45d6a0"
          strokeWidth={2}
          dot={false}
        />
      </LineChart>
    </ChartContainer>
  );
}

/* =========================================================
   RESOURCE HISTORY
========================================================= */

export function ResourceHistoryChart({
  data = [],
  height = 280
}) {
  return (
    <ChartContainer height={height}>
      <LineChart
        data={data}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        <CartesianGrid
          stroke="rgba(255,255,255,0.055)"
          vertical={false}
        />

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          minTickGap={30}
        />

        <YAxis
          domain={[0, 100]}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(value) => `${value}%`}
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value) =>
                `${Number(value).toFixed(1)}%`
              }
            />
          }
        />

        <Line
          type="monotone"
          dataKey="cpu"
          name="CPU"
          stroke="#7c9cff"
          strokeWidth={1.8}
          dot={false}
        />

        <Line
          type="monotone"
          dataKey="memory"
          name="Memory"
          stroke="#a78bfa"
          strokeWidth={1.8}
          dot={false}
        />

        <Line
          type="monotone"
          dataKey="gpu"
          name="GPU"
          stroke="#45d6a0"
          strokeWidth={1.8}
          dot={false}
        />

        <Line
          type="monotone"
          dataKey="npu"
          name="NPU"
          stroke="#e7b45c"
          strokeWidth={1.8}
          dot={false}
        />
      </LineChart>
    </ChartContainer>
  );
}

/* =========================================================
   QOS HISTORY
========================================================= */

export function QosHistoryChart({
  data = [],
  height = 280,
  target = 100
}) {
  return (
    <ChartContainer height={height}>
      <LineChart
        data={data}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        <CartesianGrid
          stroke="rgba(255,255,255,0.055)"
          vertical={false}
        />

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
        />

        <YAxis
          domain={[
            Math.max(0, target - 30),
            target + 5
          ]}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value) =>
                `${Number(value).toFixed(1)}`
              }
            />
          }
        />

        <ReferenceLine
          y={target}
          stroke="#45d6a0"
          strokeDasharray="4 4"
          strokeOpacity={0.6}
        />

        <Line
          type="monotone"
          dataKey="qos"
          name="Foreground QoS"
          stroke="#45d6a0"
          strokeWidth={2}
          dot={false}
          activeDot={{
            r: 4,
            strokeWidth: 0
          }}
        />
      </LineChart>
    </ChartContainer>
  );
}

/* =========================================================
   CONTROLLER TIMELINE
========================================================= */

export function ControllerTimeline({
  data = [],
  height = 220
}) {
  return (
    <ChartContainer height={height}>
      <LineChart
        data={data}
        margin={{
          top: 10,
          right: 10,
          left: -12,
          bottom: 0
        }}
      >
        <CartesianGrid
          stroke="rgba(255,255,255,0.055)"
          vertical={false}
        />

        <XAxis
          dataKey="time"
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          axisLine={false}
          tickLine={false}
        />

        <YAxis
          domain={[0, 3]}
          ticks={[0, 1, 2, 3]}
          tick={{
            fill: "#707887",
            fontSize: 11
          }}
          tickFormatter={(value) => {
            const labels = [
              "CONTINUE",
              "YIELD",
              "PAUSE",
              "RESUME"
            ];

            return labels[value] || "";
          }}
          axisLine={false}
          tickLine={false}
          width={62}
        />

        <Tooltip
          content={
            <ChartTooltip
              formatter={(value) => {
                const labels = [
                  "CONTINUE",
                  "YIELD",
                  "PAUSE",
                  "RESUME"
                ];

                return labels[value] || value;
              }}
            />
          }
        />

        <Line
          type="stepAfter"
          dataKey="state"
          name="Controller"
          stroke="#7c9cff"
          strokeWidth={2.2}
          dot={false}
        />
      </LineChart>
    </ChartContainer>
  );
}

/* =========================================================
   SMALL SPARKLINE
========================================================= */

export function Sparkline({
  data = [],
  dataKey = "value",
  height = 50,
  stroke = "#7c9cff",
  domain = ["auto", "auto"]
}) {
  return (
    <div className="sparkline" style={{ height }}>
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data}>
          <YAxis
            hide
            domain={domain}
          />

          <XAxis
            hide
            dataKey="time"
          />

          <Line
            type="monotone"
            dataKey={dataKey}
            stroke={stroke}
            strokeWidth={1.8}
            dot={false}
            isAnimationActive={false}
          />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}