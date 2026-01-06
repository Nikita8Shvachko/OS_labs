#pragma once

#include "common.hpp"

#include <semaphore.h>
#include <string>

sem_t *open_named_semaphore(const std::string &name, bool create);
void close_named_semaphore(const std::string &name, sem_t *sem, bool owner);

// Returns: 0 on success, 1 on timeout, -1 on error
int sem_wait_with_timeout(sem_t *sem, int seconds = WAIT_TIMEOUT_SEC);

std::string make_sem_name(const std::string &prefix, int id);
