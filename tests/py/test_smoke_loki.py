import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import smoke_loki as sl  # noqa: E402


def test_compare_reports_only_differing_fields():
    cli = {"run": "r0000", "utilisation_pct": 91.5, "jain": None}
    assert sl.compare(cli, dict(cli)) == []
    assert sl.compare(cli, {**cli, "utilisation_pct": 90.0}) == ["utilisation_pct"]
    assert sl.compare(cli, {"run": "r0000"}) == ["utilisation_pct"]  # a missing field reads as null


def test_query_helpers_speak_the_loki_http_api():
    seen = []

    class H(BaseHTTPRequestHandler):
        def do_GET(self):
            seen.append(self.path)
            body = json.dumps({"status": "success", "data": {"result": [{"values": [["1", "{}"]],
                                                                          "value": [1, "2.5"]}]}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    srv = HTTPServer(("127.0.0.1", 0), H)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    base = f"http://127.0.0.1:{srv.server_port}"
    try:
        assert sl.query_range(base, '{app="budgetcc"}')[0]["values"][0][1] == "{}"
        assert sl.query_instant(base, "vector(1)")[0]["value"][1] == "2.5"
    finally:
        srv.shutdown()
    assert seen[0].startswith("/loki/api/v1/query_range?") and "direction=forward" in seen[0]
    assert seen[1].startswith("/loki/api/v1/query?")
