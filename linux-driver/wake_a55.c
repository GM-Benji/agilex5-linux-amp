#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>

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

static int __init bm_init(void) {
    int error;
    
    bm_kobj = kobject_create_and_add("baremetal", kernel_kobj);
    if (!bm_kobj) return -ENOMEM;
    
    // Profesjonalne rejestrowanie atrybutów z obsługą błędów (usuwa warningi kompilatora)
    error = sysfs_create_file(bm_kobj, &fw_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku fw\n");
    
    error = sysfs_create_file(bm_kobj, &wake_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku wake\n");
    
    error = sysfs_create_file(bm_kobj, &log_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku log\n");
    
    error = sysfs_create_file(bm_kobj, &cmd_attr.attr);
    if (error) pr_warn("Nie udalo sie utworzyc pliku cmd\n");
    
    pr_info("=== System wstrzykiwania Bare Metal GOTOWY ===\n");
    return 0;
}

static void __exit bm_exit(void) {
    kobject_put(bm_kobj);
    pr_info("Modul usuniety.\n");
}

module_init(bm_init);
module_exit(bm_exit);
MODULE_LICENSE("GPL");
