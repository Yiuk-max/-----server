import socket, struct, json, time

def build_frame(json_str, file_data=b""):
    jb = json_str.encode('utf-8')
    total = 8 + len(jb) + len(file_data)
    return struct.pack('>II', total, len(jb)) + jb + file_data

def recv_frame(sock, timeout=3):
    sock.settimeout(timeout)
    data = b""
    while len(data) < 4:
        chunk = sock.recv(4 - len(data))
        if not chunk: return None
        data += chunk
    total = struct.unpack('>I', data)[0]
    body = b""
    while len(body) < total:
        chunk = sock.recv(total - len(body))
        if not chunk: return None
        body += chunk
    jsonlen = struct.unpack('>I', body[:4])[0]
    j = json.loads(body[4:4+jsonlen].decode('utf-8'))
    return j

print("connecting...")
s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
s.connect(('::1', 8080))
print("connected, sending register...")
uname = "持久化测试用户"
pwd = "pass123"
s.sendall(build_frame(json.dumps({"type":"register","username":uname,"password":pwd}, ensure_ascii=False)))
print("sent")
resp = recv_frame(s)
print("RESP:", resp)
s.close()
