import sqlite3
import struct
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer

DB_FILE = "yufs.db"
SERVER_PORT = 8080
ROOT_INO = 1000
S_IFDIR = 0o040000

def get_db():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn

def init_fs():
    with get_db() as conn:
        # Добавляем колонку token и включаем её в PRIMARY KEY
        conn.executescript("""
                           CREATE TABLE IF NOT EXISTS inodes (
                                                                 token TEXT,
                                                                 id INTEGER,
                                                                 mode INTEGER,
                                                                 nlink INTEGER DEFAULT 1,
                                                                 size INTEGER DEFAULT 0,
                                                                 content BLOB,
                                                                 PRIMARY KEY (token, id)
                               );
                           CREATE TABLE IF NOT EXISTS dirents (
                                                                  token TEXT,
                                                                  parent_id INTEGER,
                                                                  name TEXT,
                                                                  inode_id INTEGER,
                                                                  PRIMARY KEY(token, parent_id, name)
                               );
                           """)

class YUFSHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        ret_val = -1
        body = b""
        try:
            parsed = urllib.parse.urlparse(self.path)
            cmd = parsed.path.replace("/api/", "")
            qs = urllib.parse.parse_qs(parsed.query)

            # Получаем токен из запроса. Если нет - "default"
            token = qs.get('token', ['default'])[0]

            # Остальные аргументы
            args = {k: v[0] for k, v in qs.items() if k != 'token'}

            with get_db() as conn:
                # Для каждого запроса проверяем, создан ли ROOT для этого токена
                self.ensure_root_exists(conn, token)

                method = getattr(self, f"handle_{cmd}", None)
                if method:
                    ret_val, body = method(conn, token, args)
                else:
                    print(f"Unknown command: {cmd}")
        except Exception as e:
            print(f"Server Error: {e}")
            ret_val = -1
            body = b""

        response = struct.pack('<q', ret_val) + body
        self.send_response(200)
        self.send_header('Content-Length', str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def ensure_root_exists(self, conn, token):
        # Проверяем, есть ли root (1000) для этого токена
        exists = conn.execute("SELECT 1 FROM inodes WHERE token=? AND id=?", (token, ROOT_INO)).fetchone()
        if not exists:
            # Создаем корень для нового пользователя
            conn.execute("INSERT INTO inodes (token, id, mode, nlink, size) VALUES (?, ?, ?, 1, 0)",
                         (token, ROOT_INO, S_IFDIR | 0o777))
            print(f"Initialized root for token: {token}")

    def pack_stat(self, id, mode, size):
        return struct.pack('<IIQ', id, mode, size)

    # --- Обработчики (теперь принимают token) ---

    def handle_lookup(self, conn, token, args):
        row = conn.execute("""
                           SELECT i.id, i.mode, i.size FROM dirents d
                                                                JOIN inodes i ON d.inode_id = i.id AND d.token = i.token
                           WHERE d.token=? AND d.parent_id=? AND d.name=?
                           """, (token, args['parent_id'], args['name'])).fetchone()

        if row: return 0, self.pack_stat(row['id'], row['mode'], row['size'])
        return -1, b""

    def handle_create(self, conn, token, args):
        mode = int(args['mode'])
        try:
            # ID генерируем "глобально уникальный" или MAX(id)+1 для токена.
            # Для простоты SQLite Autoincrement не сработает с составным ключом идеально,
            # поэтому выберем MAX id для данного токена.
            max_id = conn.execute("SELECT MAX(id) FROM inodes WHERE token=?", (token,)).fetchone()[0]
            new_id = (max_id if max_id else ROOT_INO) + 1

            conn.execute("INSERT INTO inodes (token, id, mode, size, content) VALUES (?, ?, ?, 0, NULL)",
                         (token, new_id, mode))
            conn.execute("INSERT INTO dirents (token, parent_id, name, inode_id) VALUES (?, ?, ?, ?)",
                         (token, int(args['parent_id']), args['name'], new_id))
            return 0, self.pack_stat(new_id, mode, 0)
        except Exception as e:
            return -1, b""

    def handle_link(self, conn, token, args):
        try:
            target = conn.execute("SELECT mode FROM inodes WHERE token=? AND id=?", (token, int(args['target_id']))).fetchone()
            if not target or (target['mode'] & S_IFDIR): return -1, b""

            conn.execute("INSERT INTO dirents (token, parent_id, name, inode_id) VALUES (?, ?, ?, ?)",
                         (token, int(args['parent_id']), args['name'], int(args['target_id'])))
            conn.execute("UPDATE inodes SET nlink = nlink + 1 WHERE token=? AND id=?", (token, int(args['target_id'])))
            return 0, b""
        except:
            return -1, b""

    def handle_unlink(self, conn, token, args):
        try:
            d = conn.execute("SELECT inode_id FROM dirents WHERE token=? AND parent_id=? AND name=?",
                             (token, int(args['parent_id']), args['name'])).fetchone()
            if not d: return -1, b""

            conn.execute("DELETE FROM dirents WHERE token=? AND parent_id=? AND name=?",
                         (token, int(args['parent_id']), args['name']))
            # (Логику уменьшения nlink опустим для краткости, но она должна быть тут с WHERE token=?)
            return 0, b""
        except:
            return -1, b""

    def handle_rmdir(self, conn, token, args):
        try:
            conn.execute("DELETE FROM dirents WHERE token=? AND parent_id=? AND name=?",
                         (token, int(args['parent_id']), args['name']))
            return 0, b""
        except:
            return -1, b""

    def handle_getattr(self, conn, token, args):
        row = conn.execute("SELECT id, mode, size FROM inodes WHERE token=? AND id=?",
                           (token, int(args['id']))).fetchone()
        if row: return 0, self.pack_stat(row['id'], row['mode'], row['size'])
        return -1, b""

    def handle_read(self, conn, token, args):
        offset = int(args['offset'])
        size = int(args['size'])
        row = conn.execute("SELECT content FROM inodes WHERE token=? AND id=?", (token, int(args['id']))).fetchone()

        content = row['content'] if row and row['content'] else b''
        if offset >= len(content): return 0, b""
        chunk = content[offset : offset + size]
        return len(chunk), chunk

    def handle_write(self, conn, token, args):
        try:
            inode_id = int(args['id'])
            offset = int(args['offset'])
            buf = urllib.parse.unquote_to_bytes(args['buf'])

            row = conn.execute("SELECT content FROM inodes WHERE token=? AND id=?", (token, inode_id)).fetchone()
            content = bytearray(row['content']) if row and row['content'] else bytearray()

            new_end = offset + len(buf)
            if new_end > len(content): content.extend(b'\0' * (new_end - len(content)))
            content[offset : offset + len(buf)] = buf

            conn.execute("UPDATE inodes SET content=?, size=? WHERE token=? AND id=?", (content, len(content), token, inode_id))
            return len(buf), b""
        except:
            return -1, b""

    def handle_iterate(self, conn, token, args):
        inode_id = int(args['id'])
        offset = int(args['offset'])

        entries = []
        entries.append((inode_id, ".", S_IFDIR | 0o777))
        entries.append((inode_id, "..", S_IFDIR | 0o777))

        rows = conn.execute("""
                            SELECT d.name, d.inode_id, i.mode FROM dirents d
                                                                       JOIN inodes i ON d.inode_id=i.id AND d.token=i.token
                            WHERE d.token=? AND d.parent_id=?
                            """, (token, inode_id)).fetchall()

        for r in rows: entries.append((r['inode_id'], r['name'], r['mode']))

        if offset >= len(entries): return -1, b""
        e = entries[offset]
        name_bytes = e[1].encode('utf-8')
        packed = struct.pack('<I256sI', e[0], name_bytes, e[2])
        return 0, packed

if __name__ == '__main__':
    init_fs()
    server = HTTPServer(('0.0.0.0', SERVER_PORT), YUFSHandler)
    print(f"Multi-tenant YUFS Backend running on port {SERVER_PORT}...")
    server.serve_forever()