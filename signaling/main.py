import asyncio
import json
import logging
import websockets
import os

# Настройка логирования
logging.basicConfig(
    format="%(asctime)s [%(levelname)s] %(message)s",
    level=logging.INFO
)
logger = logging.getLogger(__name__)

# Простейшее хранилище для 1-к-1 соединения (PoC)
connected_peers = {
    "host": None,
    "client": None
}

def load_config():
    config_path = os.path.join(os.path.dirname(__file__), "config", "server.json")
    try:
        with open(config_path, "r") as f:
            return json.load(f)
    except FileNotFoundError:
        logger.warning("Конфиг не найден, используем дефолтные настройки.")
        return {"host": "0.0.0.0", "port": 8080}

async def signaling_handler(websocket):
    current_role = None
    
    try:
        async for message in websocket:
            data = json.loads(message)
            msg_type = data.get("type")

            # 1. Регистрация участника
            if msg_type == "register":
                role = data.get("role")
                if role in connected_peers:
                    connected_peers[role] = websocket
                    current_role = role
                    logger.info(f"Успешная регистрация: {role}")
                else:
                    logger.error(f"Неизвестная роль: {role}")
                continue

            # 2. Маршрутизация WebRTC сообщений
            if current_role and msg_type in ["offer", "answer", "ice_candidate"]:
                # Определяем получателя (если я host, шлем client'у и наоборот)
                target_role = "client" if current_role == "host" else "host"
                target_ws = connected_peers.get(target_role)

                if target_ws:
                    logger.info(f"Пересылка {msg_type} от {current_role} к {target_role}")
                    await target_ws.send(message)
                else:
                    logger.warning(f"Получатель {target_role} не подключен. Сообщение {msg_type} отброшено.")

    except websockets.ConnectionClosed:
        logger.info(f"Соединение закрыто для роли: {current_role}")
    finally:
        # Очистка при отключении
        if current_role and connected_peers[current_role] == websocket:
            connected_peers[current_role] = None
            logger.info(f"Роль освобождена: {current_role}")

async def main():
    config = load_config()
    host = config["host"]
    port = config["port"]

    logger.info(f"Запуск сигнального сервера на ws://{host}:{port}")
    
    async with websockets.serve(signaling_handler, host, port):
        await asyncio.Future()  # Бесконечный цикл работы сервера

if __name__ == "__main__":
    asyncio.run(main())