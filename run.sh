#!/bin/bash
docker stop cserver
gcc server.c -o server -Wall -Wextra -Werror -static
docker build -t c_server .

docker run --name="cserver" --rm -p 8080:80 -v $(pwd)/www:/var/www c_server
