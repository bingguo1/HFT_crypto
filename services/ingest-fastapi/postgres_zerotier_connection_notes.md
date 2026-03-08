# PostgreSQL + ZeroTier Connection Notes

Date: 2026-03-08

## What happened and fixes

### 1) `FATAL: database "abc" does not exist`

Cause: `psql` defaults to connecting to a database with the same name as the user.

Fix:

```bash
psql -U abc -d postgres
```

Then create the matching database:

```sql
CREATE DATABASE abc OWNER abc;
```

### 2) `FATAL: database "postgre" does not exist`

Cause: typo in database name.

Fix: use `postgres` (not `postgre`).

```bash
psql -U abc -d postgres
```

### 3) Remote error: `Connection refused` to `192.168.191.158:5432`

Cause: PostgreSQL was running but listening only on localhost (`127.0.0.1` / `::1`).

Fix: configure PostgreSQL to listen on the server ZeroTier IP, and allow client IP/subnet in `pg_hba.conf`.

## ZeroTier-specific configuration

Server ZeroTier IP:

- `192.168.191.158`

### `postgresql.conf`

Use server IP in `listen_addresses`:

```conf
# /etc/postgresql/14/main/postgresql.conf
listen_addresses = '127.0.0.1,192.168.191.158'
port = 5432
```

### `pg_hba.conf`

Use client IP/subnet in host rules:

```conf
# /etc/postgresql/14/main/pg_hba.conf
host    all    all    192.168.191.0/24    scram-sha-256
```

For one specific client only:

```conf
host    all    all    192.168.191.23/32   scram-sha-256
```

## Apply and verify

Restart:

```bash
sudo systemctl restart postgresql
```

Check listening addresses:

```bash
ss -ltnp '( sport = :5432 )'
```

Expected: includes `192.168.191.158:5432` (or `0.0.0.0:5432` if using `*`).

Remote test from client:

```bash
psql -h 192.168.191.158 -U abc -d abc
```

## Rule of thumb

- `listen_addresses` -> server IP(s)
- `pg_hba.conf` address column -> client IP(s) / subnet(s)

## Useful quick checks

```bash
# list databases
psql -U abc -d postgres -c "\l"

# list roles
psql -U abc -d postgres -c "\du"
```
