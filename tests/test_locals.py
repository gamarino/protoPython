class Sub:
    def send(self):
        pass
    
    print("locals inside Sub has send:", "send" in locals())
    print("locals inside Sub send value:", locals()['send'])

print("Sub.send =", getattr(Sub, 'send', None))
