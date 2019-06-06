include(CheckCSourceCompiles)

if (OpenMP_FOUND)

set(CMAKE_REQUIRED_FLAGS ${OpenMP_C_FLAGS})
set(CMAKE_REQUIRED_LIBRARIES ${OpenMP_C_LIBRARIES})
check_c_source_compiles("
#include <omp.h>

static void sink(const int x, int a[])
{
#pragma omp parallel for default(none) firstprivate(x) shared(a)
    for(int i = 0; i < 3; i++) {
        a[i] = x + i;
    }
}

int main(void)
{
    int x = 42;
    int a[3] = {0};

    sink(x, a);

    return 0;
}" HAVE_OMP_FIRSTPRIVATE_WITH_CONST)

set(CMAKE_REQUIRED_FLAGS)
set(CMAKE_REQUIRED_LIBRARIES)
endif()
