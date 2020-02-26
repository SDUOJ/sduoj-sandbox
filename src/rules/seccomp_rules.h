#ifndef JUDGER_SECCOMP_RULES_H
#define JUDGER_SECCOMP_RULES_H

#include "../stat.h"

int _c_cpp_seccomp_rules(struct config *_config, int allow_write_file);
int c_cpp_seccomp_rules(struct config *_config);
int general_seccomp_rules(struct config *_config);
int c_cpp_file_io_seccomp_rules(struct config *_config);

#endif //JUDGER_SECCOMP_RULES_H
