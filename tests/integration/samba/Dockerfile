FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends samba smbclient ca-certificates acl attr \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --no-create-home --shell /usr/sbin/nologin smbtest \
    && useradd --no-create-home --shell /usr/sbin/nologin smbguest \
    && mkdir -p /srv/samba/public/nested /srv/samba/public/private /srv/samba/archive /srv/samba/guest \
    && printf 'root fixture\n' > /srv/samba/public/root.txt \
    && printf 'nested fixture\n' > /srv/samba/public/nested/readme.txt \
    && printf 'metadata fixture\n' > /srv/samba/public/metadata.txt \
    && dd if=/dev/zero of=/srv/samba/public/large.bin bs=1M count=2 \
    && ln -s nested/readme.txt /srv/samba/public/link-to-readme.txt \
    && printf 'archive fixture\n' > /srv/samba/archive/archive.txt \
    && printf 'guest fixture\n' > /srv/samba/guest/guest.txt \
    && chown -R smbtest:smbtest /srv/samba/public /srv/samba/archive \
    && chown -R smbguest:smbguest /srv/samba/guest \
    && chmod 700 /srv/samba/public/private \
    && setfacl -m u:smbtest:rw /srv/samba/public/metadata.txt \
    && setfattr -n user.smb_browser_fixture -v synthetic /srv/samba/public/metadata.txt \
    && printf 'synthetic-password\nsynthetic-password\n' | smbpasswd -a -s smbtest

COPY smb.conf /etc/samba/smb.conf

EXPOSE 445

CMD ["smbd", "--foreground", "--no-process-group", "--debug-stdout"]
