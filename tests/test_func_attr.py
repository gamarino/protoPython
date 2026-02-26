def send(self, value): pass
print("Setting attr")
send.__isabstractmethod__ = True
print("Getting attr")
res = getattr(send, "__isabstractmethod__", False)
print("Done, res=", res)
