import sqlite3
import struct
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer

DB_FILE = "yufs.db"
SERVER_PORT = 8080
ROOT_INO = 1000

# Константы режимов (из вашего C кода)
S_IFDIR = 0o040000
S_IFREG = 0o100000

def get_db():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

def init_fs():
    with get_db() as conn:
        conn.executescript("""
                           CREATE TABLE IF NOT EXISTS inodes (
                                                                 id INTEGER PRIMARY KEY, mode INTEGER, nlink INTEGER DEFAULT 1, size INTEGER DEFAULT 0, content BLOB
                           );
                           CREATE TABLE IF NOT EXISTS dirents (
                                                                  parent_id INTEGER, name TEXT, inode_id INTEGER,
                                                                  PRIMARY KEY(parent_id, name)
                               );
                           """)
        if not conn.execute("SELECT 1 FROM inodes WHERE id=?", (ROOT_INO,)).fetchone():
            conn.execute("INSERT INTO inodes (id, mode, nlink, size) VALUES (?, ?, 1, 0)", (ROOT_INO, S_IFDIR | 0o777))

class YUFSHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Парсим URL: /api/lookup?arg=val...
        parsed = urllib.parse.urlparse(self.path)
        cmd = parsed.path.replace("/api/", "")

        # Получаем параметры (словарь списков, берем [0] элемент)
        qs = urllib.parse.parse_qs(parsed.query)
        args = {k: v[0] for k, v in qs.items()}

        ret_val = -1  # Код возврата (int64)
        body = b""    # Данные (структура или контент)

        try:
            with get_db() as conn:
                method = getattr(self, f"handle_{cmd}", None)
                if method:
                    ret_val, body = method(conn, args)
                else:
                    print(f"Unknown command: {cmd}")
        except Exception as e:
            print(f"Error in {cmd}: {e}")
            ret_val = -1

        # Отправляем ответ: 8 байт (ret_val) + данные
        # Формат <q означает little-endian int64 (стандарт для Linux x86)
        response_data = struct.pack('<q', ret_val) + body

        self.send_response(200)
        self.send_header('Content-Length', str(len(response_data)))
        self.end_headers()
        self.wfile.write(response_data)

    # --- Helpers ---
    def pack_stat(self, id, mode, size):
        # struct YUFS_stat { uint32_t id; umode_t mode; uint64_t size; };
        # Предполагаем alignment: 4 байта id, 4 байта mode (pad), 8 байт size = 16 байт
        # Если в C umode_t это short, то может быть padding. Обычно безопасно I I Q.
        return struct.pack('<IIQ', id, mode, size)

    # --- Handlers ---

    def handle_lookup(self, conn, args):
        pid = int(args['parent_id'])
        name = args['name']
        row = conn.execute("SELECT i.id, i.mode, i.size FROM dirents d JOIN inodes i ON d.inode_id = i.id WHERE d.parent_id=? AND d.name=?", (pid, name)).fetchone()
        if row:
            return 0, self.pack_stat(row['id'], row['mode'], row['size'])
        return -1, b""

    def handle_getattr(self, conn, args):
        row = conn.execute("SELECT id, mode, size FROM inodes WHERE id=?", (int(args['id']),)).fetchone()
        if row:
            return 0, self.pack_stat(row['id'], row['mode'], row['size'])
        return -1, b""

    def handle_create(self, conn, args):
        mode = int(args['mode'])
        cur = conn.execute("INSERT INTO inodes (mode, size, content) VALUES (?, 0, NULL)", (mode,))
        new_id = cur.lastrowid
        try:
            conn.execute("INSERT INTO dirents (parent_id, name, inode_id) VALUES (?, ?, ?)",
                         (int(args['parent_id']), args['name'], new_id))
            return 0, self.pack_stat(new_id, mode, 0)
        except:
            return -1, b""

    def handle_read(self, conn, args):
        inode_id = int(args['id'])
        offset = int(args['offset'])
        size = int(args['size'])

        row = conn.execute("SELECT content FROM inodes WHERE id=?", (inode_id,)).fetchone()
        if not row: return -1, b""

        content = row['content'] if row['content'] else b''
        chunk = content[offset : offset + size]
        # Возвращаем кол-во прочитанных байт как ret_val, а сами байты в body
        return len(chunk), chunk

    def handle_write(self, conn, args):
        inode_id = int(args['id'])
        offset = int(args['offset'])
        # В http.c данные кодируются в URL, надо их раскодировать обратно в байты
        # Однако http.c использует простой encode, который пропускает буквы/цифры и делает %HEX для остального
        # urllib.parse.unquote_to_bytes корректно обработает это
        buf = urllib.parse.unquote_to_bytes(args['buf'])
        size = len(buf)

        row = conn.execute("SELECT content, size FROM inodes WHERE id=?", (inode_id,)).fetchone()
        if not row: return -1, b""

        content = bytearray(row['content']) if row['content'] else bytearray()
        new_end = offset + size
        if new_end > len(content):
            content.extend(b'\0' * (new_end - len(content)))
        content[offset : offset + size] = buf

        conn.execute("UPDATE inodes SET content=?, size=? WHERE id=?", (content, len(content), inode_id))
        return size, b"" # ret_val = кол-во записанных

    def handle_unlink(self, conn, args):
        # Упрощенная логика удаления (как в прошлом примере)
        pid = int(args['parent_id'])
        name = args['name']
        d = conn.execute("SELECT inode_id FROM dirents WHERE parent_id=? AND name=?", (pid, name)).fetchone()
        if not d: return -1, b""
        conn.execute("DELETE FROM dirents WHERE parent_id=? AND name=?", (pid, name))
        # Здесь можно добавить логику уменьшения nlink и удаления inode
        return 0, b""

    def handle_rmdir(self, conn, args):
        pid = int(args['parent_id'])
        name = args['name']
        # Проверки на пустоту опущены для краткости, но должны быть тут
        conn.execute("DELETE FROM dirents WHERE parent_id=? AND name=?", (pid, name))
        return 0, b""

    def handle_iterate(self, conn, args):
        # struct YUFS_dirent { uint32_t id; char name[MAX_NAME_SIZE]; umode_t type; };
        # MAX_NAME_SIZE = 256. Align: 4 + 256 + 4 = 264 bytes.
        inode_id = int(args['id'])
        offset = int(args['offset'])

        # Собираем виртуальный список
        entries = []
        entries.append((inode_id, ".", S_IFDIR | 0o777))
        entries.append((inode_id, "..", S_IFDIR | 0o777)) # Упрощение для ..

        rows = conn.execute("SELECT d.name, d.inode_id, i.mode FROM dirents d JOIN inodes i ON d.inode_id=i.id WHERE d.parent_id=?", (inode_id,)).fetchall()
        for r in rows:
            entries.append((r['inode_id'], r['name'], r['mode']))

        if offset >= len(entries):
            return 0, b"" # Конец списка (или можно вернуть ret_val=0 и пустую структуру)

        e = entries[offset]
        # Pack: I (id), 256s (name), I (type)
        name_bytes = e[1].encode('utf-8')
        # C-строка должна быть null-terminated, pack 256s добьет нулями
        packed_dirent = struct.pack('<I256sI', e[0], name_bytes, e[2])

        # Важно: iterate в C обычно возвращает 0 при успехе, а структуру пишет в буфер
        return 0, packed_dirent

if __name__ == '__main__':
    init_fs()
    server = HTTPServer(('0.0.0.0', SERVER_PORT), YUFSHandler)
    print(f"YUFS Binary Backend running on port {SERVER_PORT}...")
    server.serve_forever()