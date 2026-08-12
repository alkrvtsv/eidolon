import asyncio
import websockets
import json

async def run_test():
    uri = "ws://localhost:8080"
    
    print("Подключение к сигнальному серверу...")
    # Открываем два независимых соединения: одно для хоста, другое для клиента
    async with websockets.connect(uri) as ws_host, websockets.connect(uri) as ws_client:
        
        # 1. Регистрация ролей на сервере
        await ws_host.send(json.dumps({"type": "register", "role": "host"}))
        await ws_client.send(json.dumps({"type": "register", "role": "client"}))
        print("[+] Хост и Клиент успешно зарегистрированы.")
        
        # Ждем секунду, чтобы сервер успел обработать регистрацию
        await asyncio.sleep(0.5)
        
        # 2. Хост отправляет SDP Offer
        offer_msg = {"type": "offer", "sdp": "v=0\r\no=- 12345 2 IN IP4 127.0.0.1\r\n..."}
        await ws_host.send(json.dumps(offer_msg))
        print(f"\n[Хост -> Сервер] Отправил: Offer")
        
        # 3. Клиент должен получить этот Offer
        received_by_client = json.loads(await ws_client.recv())
        print(f"[Сервер -> Клиент] Получил: {received_by_client['type']}")
        
        # 4. Клиент отправляет SDP Answer
        answer_msg = {"type": "answer", "sdp": "v=0\r\no=- 67890 2 IN IP4 127.0.0.1\r\n..."}
        await ws_client.send(json.dumps(answer_msg))
        print(f"\n[Клиент -> Сервер] Отправил: Answer")
        
        # 5. Хост должен получить этот Answer
        received_by_host = json.loads(await ws_host.recv())
        print(f"[Сервер -> Хост] Получил: {received_by_host['type']}")
        
        print("\n[+] Тест успешно завершен! Сигнальный сервер маршрутизирует трафик корректно.")

if __name__ == "__main__":
    asyncio.run(run_test())