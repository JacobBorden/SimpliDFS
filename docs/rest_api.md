#REST API Server

The `rest_server` executable provides a minimal HTTP interface for interacting with
SimpliDFS.  It relies on the internal `http.hpp` utilities for networking and requires a JWT in the
`Authorization` header of each request.

## Running the Server

```sh
./rest_server [port] [secret]
```
- `port` – TCP port to bind (default `8080`).
- `secret` – HMAC key used for verifying JWTs.

## Authentication

Tokens must be signed using `HS256` with the provided secret. Send the token in
`Authorization: Bearer <token>`.

## Endpoints

| Method | Path                         | Description                         |
|-------|------------------------------|-------------------------------------|
| `GET` | `/file/<name>`               | Read a file's contents.             |
| `POST`| `/file/<name>`               | Create or overwrite a file. Body is used as the file data. |
| `DELETE` | `/file/<name>`            | Remove a file.                      |
| `POST`| `/snapshot/<name>`           | Create a snapshot.                  |
| `POST`| `/snapshot/<name>/checkout`  | Restore a snapshot.                 |
| `GET` | `/snapshot`                  | List snapshot names as JSON array.  |
| `GET` | `/snapshot/<name>/diff`      | List differences from a snapshot.   |

Responses are simple JSON-encoded strings or arrays. A `401 Unauthorized`
response is returned if the JWT verification fails.
