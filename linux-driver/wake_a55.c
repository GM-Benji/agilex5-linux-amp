#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/kernel.h>

// --- DEFINICJE STRUKTURY PAMIĘCI (Musi pasować do Bare Metalu) ---
#define RING_BUFFER_SIZE 1024

typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint8_t data[RING_BUFFER_SIZE];
} RingBuffer;

typedef struct {
    RingBuffer linux_to_bm;
    RingBuffer bm_to_linux;
} SharedMemoryMap;
// -----------------------------------------------------------------

// --- DEFINICJE DLA MOSTKA FPGA ---
#define LWH2F_BASE_ADDR 0x20000000  // Adres odczytany z tabeli dla LWHPS2FPGA_memory
#define LWH2F_MAP_SIZE  0x1000      // Mapujemy jedną stronę (4KB) pamięci dla oszczędności zasobów
// -----------------------------------------------------------------

static struct kobject *bm_kobj;

// 1. Zapisywanie pliku .bin do RAM-u
static ssize_t fw_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    void __iomem *code_map = ioremap_wc(0xA0100000, count);
    if (!code_map) return -ENOMEM;
    memcpy_toio(code_map, buf, count);
    iounmap(code_map);
    pr_info("Wgrano %zu bajtow firmware'u pod adres 0xA0100000\n", count);
    return count;
}
static struct kobj_attribute fw_attr = __ATTR_WO(fw);

// 2. Budzenie rdzenia na żądanie
static ssize_t wake_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    struct arm_smccc_res res;
    arm_smccc_smc(0xC4000003, 0x101, 0xA0100000, 0, 0, 0, 0, 0, &res);
    pr_info("SMC Wake (Rdzen 0x101): %ld\n", res.a0);
    return count;
}
static struct kobj_attribute wake_attr = __ATTR_WO(wake);

// 3. Odczyt bufora z Bare Metalu
static ssize_t log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    SharedMemoryMap __iomem *map = (SharedMemoryMap __iomem *)ioremap_wc(0xA0180000, sizeof(SharedMemoryMap));
    uint32_t head, tail;
    int bytes_read = 0;

    if (!map) return -ENOMEM;

    head = readl(&map->bm_to_linux.head) % RING_BUFFER_SIZE;
    tail = readl(&map->bm_to_linux.tail) % RING_BUFFER_SIZE;

    while (tail != head && bytes_read < (PAGE_SIZE - 1)) {
        buf[bytes_read++] = readb(&map->bm_to_linux.data[tail]);
        tail = (tail + 1) % RING_BUFFER_SIZE;
    }
    
    // Zaktualizuj ogon (tail), żeby Bare Metal wiedział, że odczytaliśmy dane
    writel(tail, &map->bm_to_linux.tail);
    iounmap(map);

    buf[bytes_read] = '\n'; // Dodaj znak nowej linii dla czytelności w konsoli
    buf[bytes_read + 1] = '\0';
    return bytes_read + 1;
}
static struct kobj_attribute log_attr = __ATTR_RO(log);

// 4. Zapis komendy do bufora linux_to_bm
static ssize_t cmd_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    SharedMemoryMap __iomem *map = (SharedMemoryMap __iomem *)ioremap_wc(0xA0180000, sizeof(SharedMemoryMap));
    uint32_t head, tail, next_head;
    int i;
    
    if (!map) return -ENOMEM;
    
    head = readl(&map->linux_to_bm.head) % RING_BUFFER_SIZE;
    tail = readl(&map->linux_to_bm.tail) % RING_BUFFER_SIZE;
    
    // Wpisanie danych znak po znaku z barierami pamięci (DMB)
    for (i = 0; i < count; i++) {
        next_head = (head + 1) % RING_BUFFER_SIZE;
        if (next_head == tail) break; // Bufor pełny
        
        writeb(buf[i], &map->linux_to_bm.data[head]);
        wmb(); // Blokada - zapisz bajt ZANIM ruszysz head
        head = next_head;
    }
    writel(head, &map->linux_to_bm.head);
    
    iounmap(map);
    return count;
}
static struct kobj_attribute cmd_attr = __ATTR_WO(cmd);

// 5. Zapis danych bezposrednio na mostek LWH2F (do FPGA)
static ssize_t fpga_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    void __iomem *fpga_map;
    unsigned int offset, value;

    // Parsowanie ciągu wejściowego: oczekujemy dwóch wartości "offset wartosc"
    if (sscanf(buf, "%i %i", &offset, &value) != 2) {
        pr_err("Nieprawidlowy format. Uzyj: echo \"<offset> <wartosc>\" > fpga\n");
        return -EINVAL;
    }

    // Zabezpieczenie przed wyjściem poza zmapowaną przestrzeń 4KB
    if (offset >= LWH2F_MAP_SIZE) {
        pr_err("Przekroczono limit mapowania (offset: 0x%X, maks: 0x%X)\n", offset, LWH2F_MAP_SIZE - 4);
        return -EINVAL;
    }

    // Standardowe ioremap (zamiast _wc) ponieważ to są rejestry sprzętowe, a nie RAM
    fpga_map = ioremap(LWH2F_BASE_ADDR, LWH2F_MAP_SIZE);
    if (!fpga_map) {
        pr_err("Blad ioremap dla mostka FPGA (0x%X)\n", LWH2F_BASE_ADDR);
        return -ENOMEM;
    }

    // Zapisz wartość do FPGA
    writel(value, fpga_map + offset);
    wmb(); // Wymuś wykonanie zapisu na szynie
    
    pr_info("Wyslano na mostek LWH2F: Zapisano 0x%08X pod adres 0x%08X (offset 0x%X)\n", 
            value, LWH2F_BASE_ADDR + offset, offset);

    iounmap(fpga_map);
    return count;
}
static struct kobj_attribute fpga_attr = __ATTR_WO(fpga);


static int __init bm_init(void) {
    int error;
    
    bm_kobj = kobject_create_and_add("baremetal", kernel_kobj);
    if (!bm_kobj) return -ENOMEM;
    
    error = sysfs_create_file(bm_kobj, &fw_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku fw\n");
    
    error = sysfs_create_file(bm_kobj, &wake_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku wake\n");
    
    error = sysfs_create_file(bm_kobj, &log_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku log\n");
    
    error = sysfs_create_file(bm_kobj, &cmd_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku cmd\n");

    // Rejestracja nowego interfejsu dla mostka FPGA
    error = sysfs_create_file(bm_kobj, &fpga_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku fpga\n");
    
    pr_info("=== System wstrzykiwania Bare Metal & FPGA LWH2F GOTOWY ===\n");
    return 0;
}

static void __exit bm_exit(void) {
    kobject_put(bm_kobj);
    pr_info("Modul usuniety.\n");
}

module_init(bm_init);
module_exit(bm_exit);
MODULE_LICENSE("GPL");