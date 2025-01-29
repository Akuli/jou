import time


def aleksi(s):
    palat = []
    while len(s) >= 100:
        palat.append(s[0:100])
        s = s[100:]


def aku(s):
    palat = [s[i:i+100] for i in range(0, len(s), 100)]


s = "a" * 10_000_000

a = time.time()
aleksi(s)
b = time.time()
print("Aleksi:", (b-a)*1000, "ms")

a = time.time()
aku(s)
b = time.time()
print("Aku:", (b-a)*1000, "ms")
