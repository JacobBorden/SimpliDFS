# IPFS Gateway

The gateway exposes stored chunks over HTTP. It listens on a configurable port and serves data when the request path matches `/ipfs/<cid>`.

## Authentication

Requests must include a JWT in the `Authorization` header:

```
Authorization: Bearer <token>
```

Tokens are validated using an HMAC-SHA256 secret shared with the server. If validation fails the gateway returns `401 Unauthorized`.

## Example

```bash
curl -H "Authorization: Bearer <token>" http://localhost:8081/ipfs/<cid>
```

A successful request returns the raw chunk bytes with `application/octet-stream` content type.
