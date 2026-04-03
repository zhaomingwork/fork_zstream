import asyncio
import json
import os
import threading
import time
import unittest
import uuid
import wave

try:
    import websockets
except Exception:
    websockets = None

try:
    from ESL import ESLconnection
except Exception:
    ESLconnection = None

srv_ip="192.168.111.79"
class WebSocketCaptureServer:
    def __init__(self, host, port, output_audio_path=None, input_audio_path=None, sample_rate=8000):
        self.host = host
        self.port = port
        self.output_audio_path = output_audio_path
        self.input_audio_path = input_audio_path
        self.sample_rate = sample_rate
        self.text_messages = []
        self.binary_messages = []
        self.connected_event = threading.Event()
        self.text_event = threading.Event()
        self.binary_event = threading.Event()
        self.ready_event = threading.Event()
        self._loop = None
        self._server = None
        self._thread = None
        self._send_binary_once = False
        self._send_binary_task = None
        self._saved_output_once = False
        self._input_audio_data = None
        self._binary_payload = b"\x00\x00" * 160
        self._load_input_audio()
        self._websocket= None
 

    def _load_input_audio(self):
        if not self.input_audio_path:
            return
        if not os.path.exists(self.input_audio_path):
            return
        try:
            with open(self.input_audio_path, "rb") as f:
                f.read(44)
                self._input_audio_data = f.read()
        except Exception:
            self._input_audio_data = None

    def _append_output_audio(self, data):
        if not self.output_audio_path:
            return
        if self._saved_output_once:
            return
        out_dir = os.path.dirname(self.output_audio_path)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        try:
            if len(data) >= 12 and data[0:4] == b"RIFF" and data[8:12] == b"WAVE":
                with open(self.output_audio_path, "wb") as f:
                    f.write(data)
            else:
                with wave.open(self.output_audio_path, "wb") as wf:
                    wf.setnchannels(1)
                    wf.setsampwidth(2)
                    wf.setframerate(int(self.sample_rate))
                    wf.writeframes(data)
            self._saved_output_once = True
        except Exception:
            pass

    async def _send_binary_stream_once(self, websocket):
        if self._send_binary_once:
            return

        self._send_binary_once = True
        payload = self._input_audio_data or self._binary_payload
        chunk_size = 320
        print("send binary once", len(payload))

        try:
            for i in range(0, len(payload), chunk_size):
                await websocket.send(payload[i : i + chunk_size])
                await asyncio.sleep(0.01)
            await websocket.send(b'') #这次合成结束标记
        except Exception:
            return


    async def start():
            print("send ttsstart asrstart")
            await self._websocket.send("ttsstart")
            await self._websocket.send("asrstart")
 
    async def _handler(self, websocket):
        self.connected_event.set()
        self._websocket=websocket
        await self._websocket.send("ttsstart")
        await self._websocket.send("asrstart")
        async for message in websocket:
            print("msg====!!!===",isinstance(message, bytes))
            if isinstance(message, bytes):
                print("binary len=",len(message))
                #self._recv_byte=self._recv_byte+message
                self.binary_messages.append(message)
                total_byte=b''.join(self.binary_messages)
                print("total byte len==",len(total_byte))
 
            else:
                print("text =", message)
                if(message=="detect_end"):
                      self.binary_event.set()
                self.text_messages.append(message)
                self.text_event.set()
                if not self._send_binary_once and self._send_binary_task is None:
                    self._send_binary_task = asyncio.create_task(
                        self._send_binary_stream_once(websocket)
                    )


    async def _start(self):
        self._server = await websockets.serve(
            self._handler,
            self.host,
            self.port,
            max_size=None,
        )
        self.ready_event.set()
 

    def start(self):
        def run():
            self._loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self._loop)
            self._loop.run_until_complete(self._start())
            self._loop.run_forever()

        self._thread = threading.Thread(target=run, daemon=True)
        self._thread.start()
        self.ready_event.wait(5)

    def stop(self):
        if not self._loop or not self._server:
            return

        async def shutdown():
            await self._websocket.send("ttsstop")
            await self._websocket.send("asrstop")
            await self._websocket.send("close")
            self._server.close()
            await self._server.wait_closed()

        try:
            asyncio.run_coroutine_threadsafe(shutdown(), self._loop).result(5)
        except Exception:
            pass
        self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=5)


class forkzstreamWebSocketTest(unittest.TestCase):
    def setUp(self):
        self.ws_host = os.getenv("WS_HOST", srv_ip)
        self.ws_port = int(os.getenv("WS_PORT", "8764"))
        self.input_audio_path = os.getenv(
            "WS_INPUT_AUDIO",
            os.path.join(os.path.dirname(__file__), "asr_example.wav"),
        )
        self.output_audio_path = os.getenv(
            "WS_OUTPUT_AUDIO",
            os.path.join(os.path.dirname(__file__), "output_audio.wav"),
        )
        self.sample_rate = int(os.getenv("WS_SAMPLE_RATE", "8000"))
        os.makedirs(os.path.dirname(self.input_audio_path), exist_ok=True)
        if not os.path.exists(self.input_audio_path):
            with open(self.input_audio_path, "wb") as f:
                f.write(b"\x00\x01" * 1600)
        if os.path.exists(self.output_audio_path):
            try:
                os.remove(self.output_audio_path)
            except Exception:
                pass
        self.server = None
        if websockets is not None:
            self.server = WebSocketCaptureServer(
                self.ws_host,
                self.ws_port,
                output_audio_path=self.output_audio_path,
                input_audio_path=self.input_audio_path,
                sample_rate=self.sample_rate,
            )
            self.server.start()

    def tearDown(self):
        if self.server:
            self.server.stop()

    def test_forkzstream_esl_to_websocket(self):
        if websockets is None:
            self.skipTest("websockets 未安装")
        if ESLconnection is None:
            self.skipTest("python-esl 未安装")

        esl_host = os.getenv("FS_ESL_HOST", srv_ip)
        esl_port = os.getenv("FS_ESL_PORT", "8021")
        esl_password = os.getenv("FS_ESL_PASSWORD", "ClueCon")
        originate_target = os.getenv("FS_ORIGINATE", "user/1000 &park")

        conn = ESLconnection(esl_host, esl_port, esl_password)
        if not conn.connected():
            self.skipTest("ESL 连接失败")

        response = conn.api(f"originate {originate_target}")
        body = response.getBody() if response else ""
        body = body.strip() if body else ""
        if not body or "ERR" in body:
            self.skipTest(f"originate 失败: {body}")

        call_uuid = body.split()[-1]
        reqid = str(uuid.uuid4())
        param = {
            "callId": "callId_XXXXX",
            "tenantId": "tenantId_XXXX",
            "botid": "botid_XXXX",
            "fsInstanceId": "fsInstanceId_XXXX",
            "url": f"ws://{self.ws_host}:{self.ws_port}",
            "text": "hello",
        }

        conn.events("plain", f"CUSTOM mod_forkzstream::event {call_uuid}")
        application_args = f"{call_uuid} start {reqid} ws {json.dumps(param)}"
        conn.execute("forkzstream", application_args, call_uuid)

        text_received = self.server.text_event.wait(20)
        self.assertTrue(text_received, "未收到 WebSocket 文本消息")
      
        try:
            payload = json.loads(self.server.text_messages[0])
           # self.assertIn("context", payload)
           # self.assertIn("request", payload)
        except Exception:
            self.fail("WebSocket 文本消息不是合法 JSON")

        binary_received = self.server.binary_event.wait(20)
        #self.assertTrue(binary_received, "未收到 WebSocket 二进制音频流")
        total_byte=b''.join(self.server.binary_messages)
        print("total byte len==",len(total_byte))
                #if len(total_byte)>32000:
        self.server._append_output_audio(total_byte)
        
        
        self.tearDown()
        if self.output_audio_path:
            self.assertTrue(os.path.exists(self.output_audio_path), "未生成输出音频文件")
            self.assertGreater(os.path.getsize(self.output_audio_path), 0, "输出音频文件为空")

        try:
            conn.api(f"uuid_kill {call_uuid}")
        
        except Exception:
            pass


if __name__ == "__main__":
    unittest.main()
