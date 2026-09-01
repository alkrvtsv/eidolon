import asyncio
import json
import logging
import websockets

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

clients = {}

async def handler(websocket):
    role = None
    try:
        async for message in websocket:
            data = json.loads(message)
            msg_type = data.get("type")

            if msg_type == "register":
                role = data.get("role")
                clients[role] = websocket
                logging.info(f"Зарегистрирован: {role}")
                
                # Оповещаем хост только если подключился клиент
                if role == "client" and "host" in clients:
                    await clients["host"].send(json.dumps({"type": "start_session"}))
                continue

            target = "client" if role == "host" else "host"
            if target in clients:
                await clients[target].send(message)
                logging.info(f"Пересылка {msg_type}: {role} -> {target}")

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        if role and role in clients:
            del clients[role]
            logging.info(f"Отключен: {role}")

async def main():
    async with websockets.serve(handler, "0.0.0.0", 8080):
        logging.info("Сигнальный сервер запущен на ws://0.0.0.0:8080")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())