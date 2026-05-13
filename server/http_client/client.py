from __future__ import annotations

import json
import ssl
import sys
from dataclasses import dataclass
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin
from urllib.request import Request, urlopen


@dataclass(frozen=True)
class ResponseSnapshot:
    method: str
    url: str
    status: int
    content_type: str
    body: str


def request_text(method: str,
                 url: str,
                 token: str | None,
                 body: bytes | None = None,
                 context: ssl.SSLContext | None = None) -> ResponseSnapshot:
    headers = {}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if body is not None:
        headers["Content-Type"] = "application/json"

    request = Request(url, data=body, headers=headers, method=method)
    with urlopen(request, timeout=5, context=context) as response:
        response_body = response.read().decode("utf-8", errors="replace")
        return ResponseSnapshot(
            method=method,
            url=url,
            status=response.status,
            content_type=response.headers.get("Content-Type", ""),
            body=response_body,
        )


def print_response(snapshot: ResponseSnapshot) -> None:
    print(f"{snapshot.method} {snapshot.url}")
    print(f"status={snapshot.status} content_type={snapshot.content_type}")
    print(snapshot.body.rstrip())
    print()


def build_tls_context(args: list[str]) -> ssl.SSLContext | None:
    if "--insecure" in args:
        return ssl._create_unverified_context()

    if "--ca" in args:
        index = args.index("--ca")
        try:
            ca_path = args[index + 1]
        except IndexError as exc:
            raise ValueError("--ca requires a certificate path") from exc
        return ssl.create_default_context(cafile=ca_path)

    return None


def read_token(args: list[str]) -> str | None:
    if "--token" not in args:
        return None

    index = args.index("--token")
    try:
        return args[index + 1]
    except IndexError as exc:
        raise ValueError("--token requires a value") from exc


def read_base_url(args: list[str]) -> str:
    skip_next = False
    positional: list[str] = []
    for arg in args:
        if skip_next:
            skip_next = False
            continue
        if arg in ("--ca", "--token"):
            skip_next = True
            continue
        if arg == "--insecure":
            continue
        positional.append(arg)

    if len(positional) != 1:
        raise ValueError("usage: python -m http_client http://<esp-ip>:80 --token <token>")

    return positional[0]


def exercise_server(base_url: str, token: str | None, context: ssl.SSLContext | None) -> None:
    normalized_base = base_url.rstrip("/") + "/"
    checks = [
        ("GET", normalized_base, None),
        ("GET", urljoin(normalized_base, "health"), None),
        ("GET", urljoin(normalized_base, "api/v1/status"), None),
        (
            "POST",
            urljoin(normalized_base, "api/v1/control"),
            json.dumps({"command": "ping", "source": "espesp-http-client"}).encode("utf-8"),
        ),
    ]

    for method, url, body in checks:
        print_response(request_text(method, url, token, body, context))


def main() -> None:
    try:
        base_url = read_base_url(sys.argv[1:])
        token = read_token(sys.argv[1:])
        context = build_tls_context(sys.argv[1:])
    except ValueError as exc:
        print(exc, file=sys.stderr)
        print("HTTPS with a trusted cert: python -m http_client https://<esp-ip>:443 --token <token> --ca servercert.pem", file=sys.stderr)
        print("HTTPS self-signed quick test: python -m http_client https://<esp-ip>:443 --token <token> --insecure", file=sys.stderr)
        raise SystemExit(2)

    try:
        exercise_server(base_url, token, context)
    except HTTPError as exc:
        print(f"HTTP error: status={exc.code} reason={exc.reason}", file=sys.stderr)
        raise SystemExit(1) from exc
    except URLError as exc:
        print(f"connection failed: {exc.reason}", file=sys.stderr)
        raise SystemExit(1) from exc
    except TimeoutError as exc:
        print("connection timed out", file=sys.stderr)
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
