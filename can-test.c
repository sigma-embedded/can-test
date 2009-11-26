#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <poll.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <linux/can.h>
#include <linux/if.h>

static __attribute__ ((__always_inline__))
int spawn(char const *file, char const *arg0, ...)
{
	pid_t		pid = fork();
	int		status;

	if (pid == 0) {
		execlp(file, arg0, __builtin_va_arg_pack());
		_exit(-1);
	}

	waitpid(pid, &status, 0);
	return status == 0 ? 0 : -1;
}

static int setup_net(char const *iface)
{
	spawn("ip", "ip", "link", "set", iface, "down", NULL);
	if (spawn("ip", "ip", "link", "set", iface,
		  "type", "can", "bitrate", "100000", NULL) < 0 ||
	    spawn("ip", "ip", "link", "set", iface, "up", NULL) < 0)
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	struct sockaddr_can	addr = {
		.can_family	=  AF_CAN
	};

	int			s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	struct ifreq		ifr;
	struct pollfd		fds[2];

	if (setup_net(argv[1]) < 0)
		return EXIT_FAILURE;

	strcpy(ifr.ifr_name, argv[1]);
	ioctl(s, SIOCGIFINDEX, &ifr);

	addr.can_ifindex = ifr.ifr_ifindex;
	bind(s, (void *)&addr, sizeof addr);

	fds[0].fd     = STDIN_FILENO;
	fds[0].events = POLLIN;

	fds[1].fd     = s;
	fds[1].events = POLLIN;

	for (;;) {
		int	rc;

		fds[0].revents = 0;
		fds[1].revents = 0;

		rc = poll(fds, 2, -1);
		if (rc < 0) {
			perror("poll()");
			return EXIT_FAILURE;
		}

		if ((fds[0].revents | fds[1].revents) & POLLERR)
			return EXIT_FAILURE;

		if (fds[0].revents & POLLIN) {
			char	buf[1024];
			char	*data;
			ssize_t	l = read(fds[0].fd, buf, sizeof(buf) - 1);
			int	id;
			struct can_frame	frame;

			if (l < 0) {
				perror("read(STDIN)");
				return EXIT_FAILURE;
			}

			if (l == 0)
				/* EOF */
				break;

			buf[l] = '\0';
			while (l > 0 && iscntrl(buf[l-1]))
				buf[--l] = '\0';

			id = strtol(buf, &data, 0);
			if (data[0] == '\0') {
				fprintf(stderr, "missing data in input\n");
				continue;
			}

			++data;

			if (id < 0) {
				id -= id;
				id |= CAN_RTR_FLAG;
			}

			if (id > (1 << 11) - 1) {
				id &= CAN_EFF_MASK | CAN_RTR_FLAG;
				id |= CAN_EFF_FLAG;
			}

			frame.can_dlc = MIN(strlen(data), sizeof frame.data);
			frame.can_id = id;
			memcpy(frame.data, data, frame.can_dlc);

			rc = sendto(s, &frame, sizeof frame, 0,
				    (void const *)&addr, sizeof addr);
			if (rc < 0) {
				perror("sendto(CAN)");
				return EXIT_FAILURE;
			}
		}

		if (fds[1].revents & POLLIN) {
			struct can_frame	frame;
			struct sockaddr_can	r_addr;
			socklen_t		a_len = sizeof r_addr;
			size_t			i;
			uint32_t		id;

			rc = recvfrom(s, &frame, sizeof frame, 0,
				      (void *)&r_addr, &a_len);
			if (rc < 0) {
				perror("recvfrom(CAN)");
				return EXIT_FAILURE;
			}

			if (rc == 0)
				/* EOF? */
				break;

			id = frame.can_id;
			if (!(frame.can_id & CAN_EFF_FLAG))
				id &= CAN_SFF_MASK;
			else
				id &= CAN_EFF_MASK;

			printf("[%d]: %s%s%u[%u] = [",
			       r_addr.can_ifindex,
			       frame.can_id & CAN_EFF_FLAG ? "E" : "",
			       frame.can_id & CAN_RTR_FLAG ? "R" : "",
			       id,
			       frame.can_dlc);

			for (i = 0; i < MIN(frame.can_dlc,
					    sizeof frame.data); ++i) {
				unsigned char	c = frame.data[i];

				if (i > 0)
					printf(",");

				if (isprint(c))
					printf("'%c'", c);
				else
					printf("%02x", c);
			}

			printf("]\n");
		}
	}
}
