import asyncio
import websockets
import json

async def run_client():
    uri = "ws://127.0.0.1:8080"
    try:
        async with websockets.connect(uri) as ws:
            print("[Клиент] Подключился к серверу.")
            
            # 1. Регистрируемся как клиент
            await ws.send(json.dumps({"type": "register", "role": "client"}))
            print("[Клиент] Успешная регистрация.")
            
            # 2. Отправляем фейковый WebRTC Offer
            fake_offer = {
                "type": "offer",
                "sdp": (
                    "v=0\r\n"
                    "o=- 1681234567 2 IN IP4 127.0.0.1\r\n"
                    "s=-\r\n"
                    "t=0 0\r\n"
                    "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                    "c=IN IP4 0.0.0.0\r\n"
                    "a=setup:actpass\r\n"
                    "a=mid:0\r\n"
                    "a=recvonly\r\n"
                    "a=ice-ufrag:testufrag123\r\n"
                    "a=ice-pwd:testpassword123456789012\r\n"
                    "a=fingerprint:sha-256 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\r\n"
                    "a=rtpmap:96 H264/90000\r\n"
                )
            }
            await ws.send(json.dumps(fake_offer))
            print("[Клиент] SDP Offer отправлен. Жду ответа от C++ Хоста...")
            
            # 3. Ждем ответа (Answer) от C++
            response = await ws.recv()
            print(f"\n[УСПЕХ! Ответ от C++ получен]:\n{response[:200]}...")
            
    except Exception as e:
        print(f"[-] Ошибка: {e}")

if __name__ == "__main__":
    asyncio.run(run_client())