import os

# Настройки исключений
OUTPUT_FILENAME = "project_dump.txt"
EXCLUDE_DIRS = {".git", ".vscode", "build", "vcpkg", "__pycache__", "venv", ".venv", "env"}
EXCLUDE_EXTENSIONS = {".md"}

# Получаем имя текущего запущенного скрипта
CURRENT_SCRIPT_NAME = os.path.basename(__file__)

# Файлы, которые должны быть видны в структуре (дереве), но их КОДИРОВАТЬ НЕ НУЖНО
EXCLUDE_CONTENT_FILES = {
    "host/src/nvenc/nvEncodeAPI.h",
    "nvEncodeAPI.h"
}

EXCLUDE_FILES = {OUTPUT_FILENAME, CURRENT_SCRIPT_NAME}

def should_exclude_dir(dirname):
    return dirname in EXCLUDE_DIRS

def should_exclude_file(filename):
    if filename in EXCLUDE_FILES:
        return True
    ext = os.path.splitext(filename)[1].lower()
    if ext in EXCLUDE_EXTENSIONS:
        return True
    return False

def print_tree(start_path, prefix=""):
    """Рекурсивно генерирует текстовое дерево папок и файлов (nvEncodeAPI.h здесь останется)."""
    tree_lines = []
    try:
        items = sorted(os.listdir(start_path))
    except PermissionError:
        return []

    filtered_items = []
    for item in items:
        path = os.path.join(start_path, item)
        if os.path.isdir(path):
            if should_exclude_dir(item):
                continue
        else:
            # Для дерева проверяем только базовые исключения (без EXCLUDE_CONTENT_FILES, чтобы файл остался в структуре)
            if item in EXCLUDE_FILES or os.path.splitext(item)[1].lower() in EXCLUDE_EXTENSIONS:
                continue
        filtered_items.append(item)

    for i, item in enumerate(filtered_items):
        path = os.path.join(start_path, item)
        is_last = (i == len(filtered_items) - 1)
        connector = "└── " if is_last else "├── "
        
        tree_lines.append(f"{prefix}{connector}{item}")
        
        if os.path.isdir(path):
            extension = "    " if is_last else "│   "
            tree_lines.extend(print_tree(path, prefix + extension))
            
    return tree_lines

def export_project():
    root_dir = os.path.abspath(os.path.dirname(__file__))
    output_path = os.path.join(root_dir, OUTPUT_FILENAME)
    
    print(f"Сканирование проекта из: {root_dir}")
    
    with open(output_path, "w", encoding="utf-8") as out_f:
        # 1. Записываем структуру проекта (дерево будет полным, включая nvEncodeAPI.h)
        out_f.write("=== СТРУКТУРА ПРОЕКТА ===\n")
        root_name = os.path.basename(root_dir)
        out_f.write(f"{root_name}/\n")
        tree = print_tree(root_dir)
        for line in tree:
            out_f.write(line + "\n")
        out_f.write("\n" + "="*50 + "\n\n=== СОДЕРЖИМОЕ ФАЙЛОВ ===\n\n")
        
        # 2. Обходим файлы и копируем их содержимое (исключая nvEncodeAPI.h из текста)
        file_counter = 1
        for dirpath, dirnames, filenames in os.walk(root_dir, topdown=True):
            dirnames[:] = [d for d in dirnames if not should_exclude_dir(d)]
            
            for filename in filenames:
                if should_exclude_file(filename):
                    continue
                
                full_path = os.path.join(dirpath, filename)
                rel_path = os.path.relpath(full_path, root_dir)
                formatted_path = rel_path.replace(os.sep, '/')
                
                # Проверяем, нужно ли пропустить копирование содержимого для nvEncodeAPI.h
                if formatted_path in EXCLUDE_CONTENT_FILES or filename in EXCLUDE_CONTENT_FILES:
                    out_f.write(f"{file_counter}. *{formatted_path}*\n")
                    out_f.write("```\n[Содержимое файла пропущено по условию]\n```\n\n")
                    file_counter += 1
                    continue
                
                out_f.write(f"{file_counter}. *{formatted_path}*\n")
                out_f.write("```\n")
                
                try:
                    with open(full_path, "r", encoding="utf-8", errors="ignore") as in_f:
                        out_f.write(in_f.read())
                except Exception as e:
                    out_f.write(f"[Ошибка чтения файла: {e}]\n")
                    
                out_f.write("\n```\n\n")
                file_counter += 1

    print(f"Готово! Содержимое сохранено в файл: {OUTPUT_FILENAME}")

if __name__ == "__main__":
    export_project()