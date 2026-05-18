FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends samba smbclient ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --no-create-home --shell /usr/sbin/nologin smbtest \
    && mkdir -p /srv/samba/public \
    && chown -R smbtest:smbtest /srv/samba/public \
    && printf 'synthetic-password\nsynthetic-password\n' | smbpasswd -a -s smbtest

COPY smb.conf /etc/samba/smb.conf

EXPOSE 445

CMD ["smbd", "--foreground", "--no-process-group", "--debug-stdout"]
