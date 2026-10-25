def connect(host, port=80, *, timeout=10):
    return (host, port, timeout)


connect("example.org", timeout=5)
