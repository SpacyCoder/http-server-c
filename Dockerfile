FROM ubuntu:latest AS builder
RUN useradd spacy -u 1001 -U; mkdir -p /var/webserver;

FROM scratch
COPY . /
COPY --from=builder /etc/passwd /etc/passwd
COPY --from=builder /var/webserver /var/webserver
EXPOSE 80
CMD ["/server"]
