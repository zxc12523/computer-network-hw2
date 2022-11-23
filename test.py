import threading
import signal
import time
import os
from subprocess import Popen, PIPE, STDOUT

TEST_NUM = 100
FILE_SIZE = 1024 * 1024 * 15


def createFile():
    for i in range(TEST_NUM):
        with open(os.path.join("client_dir", f"file{i:03}"), "wb") as fout:
            fout.write(os.urandom(FILE_SIZE))


def putFile(index):
    p = Popen(["./client", "Alice", "127.0.0.1:9655"],
              stdin=PIPE, stdout=PIPE, stderr=None)
    time.sleep(3)
    print(f"{index}: start")
    p.stdin.write(f"put file{index:03}\n".encode('utf-8'))
    p.stdin.flush()

    time.sleep(300)
    p.send_signal(signal.SIGINT)


if __name__ == "__main__":
    createFile()
    thread_list = []
    for i in range(TEST_NUM):
        t = threading.Thread(target=putFile, args=(i, ))
        thread_list.append(t)
    for t in thread_list:
        t.start()
    for t in thread_list:
        t.join()
