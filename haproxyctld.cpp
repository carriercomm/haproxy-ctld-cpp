#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <sstream>

#define STATUS_CMD "show stat\n"
#define BUFLEN 1024
#define MAX_SESSIONS_PER_GEAR 16
#define GEAR_UP_PCT 90.0

const char *app_name;
const char *gear_uuid;
const char *haproxy_dir;
char *gear_namespace;
char cmd[300];

void parse_data(const std::string &data)
{
    std::cout << "Raw data:" << std::endl;
    std::cout << data << std::endl;
    std::stringstream ss(data);
    std::string line;
    int scount = 0;
    int scur = -1;

    while (std::getline(ss, line)) {
        if (!line.find("express")) {
            scount++;
            if (line.find("BACKEND") != std::string::npos) {
                std::string token;
                std::stringstream sl(line);
                int i = 0;
                while (std::getline(sl, token, ',')) {
                    //std::cout << token << std::endl;
                    i++;
                    if (i == 5) {
                        scur = atoi(token.c_str());
                        break;
                    }
                }
            }
        }
    }

    int num_gears = scount - 2;
    std::cout << "Number of servers: " << num_gears << std::endl;
    std::cout << "Number of concurrent sessions: " << scur << std::endl << std::endl;

    float sessions_per_gear = scur / num_gears;
    float sessions_capacity_pct = (sessions_per_gear / MAX_SESSIONS_PER_GEAR) * 100;

    if (sessions_capacity_pct > GEAR_UP_PCT) {
        std::cout << "SCALE UP" << std::endl;
        snprintf(cmd, 300, "%s/usr/bin/add-gear -n %s -a %s -u %s 2>&1", haproxy_dir,
                                gear_namespace,
                                app_name,
                                gear_uuid);
        std::cout << cmd << std::endl;
        int ret = system(cmd);
        if (ret == -1) {
            fprintf(stderr, "Failed to execute scale up.\n");
        } else {
            int status = WEXITSTATUS(ret);
            if (status != 0) {
                fprintf(stderr, "Failed to scale up.\n");
            } else {
                printf("Scale up succeeded.\n");
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int s, t, len;
    struct sockaddr_un remote;

    char str[BUFLEN];
    char sock_path[256] = { '\0' };

    haproxy_dir = getenv("OPENSHIFT_HAPROXY_DIR");
    app_name = getenv("OPENSHIFT_APP_NAME");
    gear_uuid = getenv("OPENSHIFT_GEAR_UUID");
    const char *gear_dns = getenv("OPENSHIFT_GEAR_DNS");
    const char *beg = strchr(gear_dns, '-');
    const char *end = strchr(gear_dns, '.');
    gear_namespace = (char *)malloc(end - beg + 1);
    strncpy(gear_namespace, beg + 1, end - beg - 1);
    std::cout << "Gear dns:" << gear_dns << std::endl;
    std::cout << "Gear namespace:" << gear_namespace << std::endl;
    snprintf(sock_path, 256, "%s/run/stats", haproxy_dir);
    signal(SIGPIPE, SIG_IGN);

    while (1) {
        if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(1);
        }

        std::cout << "Gathering stats from HAProxy" << std::endl;
        std::cout << "============================================" << std::endl << std::endl;


        remote.sun_family = AF_UNIX;
        strcpy(remote.sun_path, sock_path);
        len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        if (connect(s, (struct sockaddr *)&remote, len) == -1) {
            perror("connect");
            exit(1);
        }

        //printf("Connected!\n");
        if (send(s, STATUS_CMD, strlen(STATUS_CMD), 0) == -1) {
            perror("send");
            exit(1);
        }

        std::string data = "";
        while ((t = recv(s, str, BUFLEN, 0)) > 0) {
            //write(STDOUT_FILENO, str, t);
            data.append(str, t);
        }
        parse_data(data);

        if (t < 0) perror("recv");

        close(s);
        sleep(3);
    }

    return 0;
}

