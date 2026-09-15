#ifndef _CHILD_H_
#define _CHILD_H_

class AppConfig;

int RunChild(const AppConfig &cfg, int parent_fd, int control_fd);

#endif /* _CHILD_H_ */
