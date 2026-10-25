async def fetch(source):
    async with source as s:
        return await s.read()


async def collect(stream):
    out = []
    async for item in stream:
        out.append(item)
    return out
