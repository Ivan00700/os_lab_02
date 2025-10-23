#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// -------------------- Глобальные переменные --------------------

// Максимальное число потоков, разрешённых к запуску одновременно
int max_threads;

// Текущее число активных потоков
int current_threads = 0;

// Максимальное число потоков, которые одновременно были активны
int peak_threads = 0;

// Мьютекс для синхронизации доступа к счётчикам потоков
pthread_mutex_t lock;

// -------------------- Структура для аргументов потока --------------------
typedef struct {
    int *arr;   // указатель на массив
    int left;   // левый индекс сегмента массива
    int right;  // правый индекс сегмента массива
} thread_args;

// -------------------- Функция слияния двух отсортированных половин --------------------
void merge(int *arr, int left, int mid, int right) {
    int n1 = mid - left + 1;
    int n2 = right - mid;

    // Временные массивы для левой и правой половины
    int *L = malloc(n1 * sizeof(int));
    int *R = malloc(n2 * sizeof(int));

    for (int i = 0; i < n1; i++) L[i] = arr[left + i];
    for (int j = 0; j < n2; j++) R[j] = arr[mid + 1 + j];

    int i = 0, j = 0, k = left;

    // Слияние двух массивов в исходный
    while (i < n1 && j < n2)
        arr[k++] = (L[i] <= R[j]) ? L[i++] : R[j++];

    while (i < n1) arr[k++] = L[i++];
    while (j < n2) arr[k++] = R[j++];

    free(L);
    free(R);
}

// -------------------- Последовательная сортировка слиянием --------------------
void merge_sort_seq(int *arr, int left, int right) {
    if (left >= right) return; // сегмент из 1 элемента уже отсортирован
    int mid = (left + right) / 2;
    merge_sort_seq(arr, left, mid);       // сортировка левой половины
    merge_sort_seq(arr, mid + 1, right);  // сортировка правой половины
    merge(arr, left, mid, right);         // слияние двух половин
}

// -------------------- Многопоточная сортировка --------------------
void *merge_sort_thread(void *args) {
    // Получаем аргументы из структуры
    thread_args *data = (thread_args *)args;
    int left = data->left;
    int right = data->right;
    int *arr = data->arr;
    free(data); // структура больше не нужна

    // Базовый случай рекурсии: сегмент из 0 или 1 элемента
    if (left >= right) {
        pthread_mutex_lock(&lock);   // блокируем доступ к счётчику потоков
        current_threads--;           // уменьшаем число активных потоков
        pthread_mutex_unlock(&lock); // разблокируем
        return NULL;
    }

    int mid = (left + right) / 2;    // делим сегмент на две половины
    pthread_t tid1 = 0, tid2 = 0;    // идентификаторы потоков
    int created1 = 0, created2 = 0;  // флаги создания потоков

    // -------------------- Левая половина --------------------
    thread_args *arg1 = malloc(sizeof(thread_args));
    arg1->arr = arr;
    arg1->left = left;
    arg1->right = mid;

    pthread_mutex_lock(&lock); // блокируем счётчик потоков
    if (current_threads < max_threads) { // проверяем лимит
        current_threads++;                // увеличиваем число активных потоков
        if (current_threads > peak_threads) peak_threads = current_threads; // обновляем максимум
        created1 = 1;                     // отмечаем, что поток можно создать
    }
    pthread_mutex_unlock(&lock); // разблокируем

    if (created1)
        pthread_create(&tid1, NULL, merge_sort_thread, arg1); // создаём поток
    else {
        merge_sort_seq(arr, left, mid); // сортировка последовательно
        free(arg1);
    }

    // -------------------- Правая половина --------------------
    thread_args *arg2 = malloc(sizeof(thread_args));
    arg2->arr = arr;
    arg2->left = mid + 1;
    arg2->right = right;

    pthread_mutex_lock(&lock);
    if (current_threads < max_threads) {
        current_threads++;
        if (current_threads > peak_threads) peak_threads = current_threads;
        created2 = 1;
    }
    pthread_mutex_unlock(&lock);

    if (created2)
        pthread_create(&tid2, NULL, merge_sort_thread, arg2);
    else {
        merge_sort_seq(arr, mid + 1, right);
        free(arg2);
    }

    // -------------------- Ожидание завершения дочерних потоков --------------------
    if (created1) pthread_join(tid1, NULL);
    if (created2) pthread_join(tid2, NULL);

    // -------------------- Слияние двух половин --------------------
    merge(arr, left, mid, right);

    // -------------------- Поток завершил работу --------------------
    pthread_mutex_lock(&lock);
    current_threads--;           // уменьшаем число активных потоков
    pthread_mutex_unlock(&lock);

    return NULL;
}

// -------------------- Копирование массива --------------------
void copy_array(int *src, int *dst, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

// -------------------- Главная функция --------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Использование: %s <размер массива>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);              // размер массива
    int *original = malloc(n * sizeof(int));
    srand(time(NULL));

    // Генерация случайного массива
    for (int i = 0; i < n; i++)
        original[i] = (rand() % 1000) + 1;

    // Вывод первых 10 элементов исходного массива
    printf("Исходный массив (первые 10 элементов): ");
    for (int i = 0; i < (n < 10 ? n : 10); i++)
        printf("%d ", original[i]);
    printf("\n");

    int num_cores = sysconf(_SC_NPROCESSORS_ONLN); // количество логических ядер
    int threads_list[] = {1, 2, 4, 8, 16};

    printf("\nЭксперимент с разным числом потоков (макс %d):\n", num_cores);
    printf("Потоки\tВремя (с)\tУскорение\tЭффективность (%%)\n");

    double t1 = 0; // время для 1 потока

    // Цикл по разным числам потоков
    for (int i = 0; i < 5; i++) {
        int t = threads_list[i];
        if (t > num_cores) t = num_cores; // ограничение числом ядер

        int *arr = malloc(n * sizeof(int));
        copy_array(original, arr, n); // копируем массив для честного сравнения

        max_threads = t;
        current_threads = 1; // основной поток уже работает
        peak_threads = 1;
        pthread_mutex_init(&lock, NULL);

        // Создание главного потока сортировки
        pthread_t tid_main;
        thread_args *args = malloc(sizeof(thread_args));
        args->arr = arr;
        args->left = 0;
        args->right = n - 1;

        clock_t start = clock();
        pthread_create(&tid_main, NULL, merge_sort_thread, args);
        pthread_join(tid_main, NULL);
        clock_t end = clock();

        double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        if (i == 0) t1 = time_spent; // время для 1 потока
        double speedup = t1 / time_spent;           // ускорение
        double efficiency = speedup / t * 100.0;   // эффективность

        printf("%d\t%.4f\t\t%.2f\t\t%.2f%%", t, time_spent, speedup, efficiency);

        // Для 1 потока выводим первые 10 отсортированных элементов
        if (t == 16) {
            printf("\n\nОтсортированные первые 10 элементов: ");
            for (int j = 0; j < (n < 10 ? n : 10); j++)
                printf("%d ", arr[j]);
            printf("\n");
        }
        printf("\n");

        pthread_mutex_destroy(&lock);
        free(arr);
    }

    free(original);
    return 0;
}
