# Docker Samba integration tests

These tests are disabled by default and use only synthetic credentials.

1. Start the Samba fixture:

```bash
make samba-up
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
make samba-down
```

Default connection settings:

- server: `127.0.0.1:1445`
- share: `public`
- username: `smbtest`
- password: `synthetic-password`

Fixture shares:

- `public`: password-protected read/write share with nested directory,
  `root.txt`, `metadata.txt`, `large.bin`, a private directory and a symlink
  fixture.
- `archive`: second password-protected share for cross-share copy/move tests.
- `guest`: guest-access read/write share.

If the local SMB backend cannot use a host:port server string, run the fixture
on an address/port supported by the platform and override the test environment
variables in CTest.

The default integration build uses the clean-room native SMB backend. The test
profile remains opt-in and must use synthetic credentials only.

Native real-wire validation is gated so the default local test run remains
offline-only. To run it explicitly:

```bash
SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 \
ctest --test-dir tmp/build-samba -L docker-samba --output-on-failure
```
