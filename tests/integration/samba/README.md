# Docker Samba integration tests

These tests are disabled by default and use only synthetic credentials.

1. Start the Samba fixture:

```bash
docker compose -f tests/integration/samba/docker-compose.yml up -d --build
```

2. Configure a separate build with the integration profile enabled:

```bash
cmake -S . -B tmp/build-samba -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=ON
cmake --build tmp/build-samba
```

3. Run only the Docker Samba profile:

```bash
ctest --test-dir tmp/build-samba -L docker-samba --output-on-failure
```

4. Stop the fixture:

```bash
docker compose -f tests/integration/samba/docker-compose.yml down -v
```

Default connection settings:

- server: `127.0.0.1:1445`
- share: `public`
- username: `smbtest`
- password: `synthetic-password`

If the local SMB backend cannot use a host:port server string, run the fixture
on an address/port supported by the platform and override the test environment
variables in CTest.
