#!/usr/bin/env python3
"""Opens the MONOCORE-1 simulator through a small local web server.

Double-click (or: python simulateur.py). The page is served at http://localhost:<port>/monocore.html and
opened in the default browser. Served this way, the page may use the MIDI keyboards in Firefox too,
which refuses Web MIDI to a file opened directly. Close this window to stop the server.
"""
import functools, http.server, os, socket, threading, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))

def free_port(first=8000):
    for port in range(first, first + 50):
        with socket.socket() as s:
            try:
                s.bind(('127.0.0.1', port)); return port
            except OSError:
                continue
    return 0

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args): pass

port = free_port()
server = http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(Quiet, directory=HERE))
port = server.server_address[1]
url = 'http://localhost:%d/monocore.html' % port
print('Simulateur MONOCORE-1 :', url)
print('Fermez cette fenetre (ou Ctrl+C) pour arreter le serveur.')
threading.Timer(0.5, lambda: webbrowser.open(url)).start()
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
