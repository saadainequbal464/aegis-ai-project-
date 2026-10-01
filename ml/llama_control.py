import json
import time
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError


class LlamaControl:

    def __init__(
        self,
        server_url="http://127.0.0.1:8080",
        slot_id=0,
        slot_filename="aegis_pause_slot.bin",
    ):
        self.server_url = server_url.rstrip("/")
        self.slot_id = slot_id
        self.slot_filename = slot_filename
        self.state = "RUNNING"
        self.last_prompt = None
        self.last_completion_id = None
        self.last_request = None

    def request(self, method, path, body=None):

        url = self.server_url + path

        data = None
        headers = {}

        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"

        req = Request(
            url,
            data=data,
            headers=headers,
            method=method,
        )

        try:
            with urlopen(req, timeout=10) as response:
                raw = response.read().decode("utf-8")

                if not raw:
                    return {}

                return json.loads(raw)

        except HTTPError as e:
            raw = e.read().decode("utf-8", errors="replace")
            raise RuntimeError(
                f"HTTP {e.code}: {raw}"
            )

        except URLError as e:
            raise RuntimeError(
                f"Connection failed: {e}"
            )

    def save_slot(self):

        result = self.request(
            "POST",
            f"/slots/{self.slot_id}?action=save",
            {
                "filename": self.slot_filename
            },
        )

        if result.get("n_saved", 0) <= 0:
            raise RuntimeError(
                f"KV cache was not saved: {result}"
            )

        return result

    def cancel_generation(self, completion_id):

        if not completion_id:
            raise ValueError(
                "completion_id is required"
            )

        return self.request(
            "DELETE",
            f"/v1/stream?conv_id={completion_id}",
        )

    def restore_slot(self):

        result = self.request(
            "POST",
            f"/slots/{self.slot_id}?action=restore",
            {
                "filename": self.slot_filename
            },
        )

        if result.get("n_restored", 0) <= 0:
            raise RuntimeError(
                f"KV cache was not restored: {result}"
            )

        return result

    def start_generation(
        self,
        messages,
        max_tokens=512,
        stream=True,
    ):

        conversation_id = (
            f"aegis-{int(time.time() * 1000)}"
        )

        payload = {
            "model": "gemma-3-4b-instruct-gguf",
            "messages": messages,
            "max_tokens": max_tokens,
            "stream": stream,
        }

        headers = {
            "Content-Type": "application/json",
            "X-Conversation-Id": conversation_id,
        }

        data = json.dumps(payload).encode("utf-8")

        req = Request(
            self.server_url + "/v1/chat/completions",
            data=data,
            headers=headers,
            method="POST",
        )

        response = urlopen(req, timeout=30)

        self.last_completion_id = conversation_id
        self.last_prompt = messages
        self.last_request = payload
        self.state = "RUNNING"

        return response

    def pause(self):

        if self.state == "PAUSED":
            return {
                "action": "PAUSE",
                "state": "PAUSED",
            }

        if not self.last_completion_id:
            raise RuntimeError(
                "No active generation to pause"
            )

        save_result = self.save_slot()

        cancel_result = self.cancel_generation(
            self.last_completion_id
        )

        self.state = "PAUSED"

        return {
            "action": "PAUSE",
            "state": self.state,
            "save": save_result,
            "cancel": cancel_result,
        }

    def resume(self):

        if self.state != "PAUSED":
            return {
                "action": "RESUME",
                "state": self.state,
            }

        restore_result = self.restore_slot()

        self.state = "RUNNING"

        return {
            "action": "RESUME",
            "state": self.state,
            "restore": restore_result,
        }

    def apply_controller_action(
        self,
        action,
        completion_id=None,
    ):

        action = action.upper()

        if action == "CONTINUE":
            self.state = "RUNNING"

            return {
                "action": "CONTINUE",
                "state": self.state,
            }

        if action == "PAUSE":

            if completion_id:
                self.last_completion_id = completion_id

            return self.pause()

        if action == "RESUME":
            return self.resume()

        if action == "YIELD":
            return {
                "action": "YIELD",
                "state": self.state,
            }

        raise ValueError(
            f"Unknown controller action: {action}"
        )


if __name__ == "__main__":

    controller = LlamaControl(
        server_url="http://127.0.0.1:8080",
        slot_id=0,
        slot_filename="aegis_pause_slot.bin",
    )

    print("AEGIS LLM control bridge ready")

    print(
        controller.apply_controller_action(
            "CONTINUE"
        )
    )