#!/usr/bin/env python3
"""Local FTP server for the ordered-mirror test.

Every download waits a random 0..DELAY ms before sending, like files on a
real network finishing in random order. Anonymous, read-only.

usage: ftp_server.py ROOT PORT [DELAY_MS] [SEED]
"""
import random
import sys
import time

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import ThreadedFTPServer

root, port = sys.argv[1], int(sys.argv[2])
delay_ms = int(sys.argv[3]) if len(sys.argv) > 3 else 300
rng = random.Random(int(sys.argv[4]) if len(sys.argv) > 4 else 1)


class SlowHandler(FTPHandler):
    def ftp_RETR(self, file):
        time.sleep(rng.uniform(0, delay_ms) / 1000.0)
        return super().ftp_RETR(file)


auth = DummyAuthorizer()
auth.add_anonymous(root, perm="elr")
SlowHandler.authorizer = auth
SlowHandler.passive_ports = range(60000, 60500)
server = ThreadedFTPServer(("127.0.0.1", port), SlowHandler)
server.max_cons = 256
server.serve_forever()
