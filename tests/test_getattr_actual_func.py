def send(self, value):
    """Send a value into the coroutine.
    Return next yielded value or raise StopIteration.
    """
    raise StopIteration

print("Getting getattr...")
getattr(send, "__isabstractmethod__", False)
print("Done")
