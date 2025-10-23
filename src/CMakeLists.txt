cmake_minimum_required(VERSION 3.10)

# Название проекта
project(OS_Lab_02 C)

# Используем стандарт C11
set(CMAKE_C_STANDARD 11)

# Включаем поддержку pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Добавляем исполняемый файл
add_executable(main main.c)

# Линкуем с pthread
target_link_libraries(main PRIVATE Threads::Threads)
