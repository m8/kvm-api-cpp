#include <stdint.h>
#include <linux/kvm.h>
#include <sys/ioctl.h> /* For ioctl */
#include <sys/mman.h>  /* For mmap */
#include <fcntl.h>     /* For file descriptor operations */
#include <string>
#include <cassert>
#include "unistd.h"


#define RAM_SIZE 512000000
#define CODE_START 0x1000

struct vm
{
    uint16_t kvm_fd = 0;
    uint16_t vm_fd = 0;
    uint64_t mem_start = 0;
    struct vcpu *vcpu = nullptr;
};

struct vcpu
{
    uint16_t vcpu_fd;
    uint32_t kvm_run_mmap_size;

    pthread_t vcpu_thread;
    struct kvm_run *kvm_run;
    struct kvm_regs regs;   // General purpose registers from the vcpu
    struct kvm_sregs sregs; // Special registers in the vcpu.
    void *(*vcpu_thread_func)(void *);
};
/*
-- kvm_run structure -- 
Used to communicate information about the CPU between the kernel and user space
*/




/*
    To create kvm we need a init function
    Function will print kvm version
*/
struct vm *kvm_init(void)
{
    vm *kvmq = new vm();
    kvmq->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);

    if (kvmq->kvm_fd < 0)
    {
        perror("CAN NOT OPEN KVM\n");
        return NULL;
    }

    uint8_t ver = ioctl(kvmq->kvm_fd, KVM_GET_API_VERSION, 0);
    
    if (ver != 12){
	    fprintf(stderr, "KVM_GET_API_VERSION %d, expected 12",ver);
        return NULL;
    }

    fprintf(stdout, "KVM version %d\n", ver);
    return kvmq;
}

int kvm_create_vm(struct vm *kvm)
{
    struct kvm_userspace_memory_region mem;

    kvm->vm_fd = ioctl(kvm->kvm_fd, KVM_CREATE_VM, 0);
    if (kvm->vm_fd < 0)
    {
        perror("ERROR KVM_CREATE_VM");
        return -1;
    }

    /*
    + PROT_READ: Page can be read.
    + PROT_WRITE: Page can be written.
    */
    kvm->mem_start = (uint64_t)mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if ((void *)kvm->mem_start == MAP_FAILED)
    {
        perror("ERROR MAP_FAILED");
        return -1;
    }

    mem.flags = 0;
    mem.slot = 0;
    mem.guest_phys_addr = 0;
    mem.memory_size = RAM_SIZE;
    mem.userspace_addr = (uint64_t)kvm->mem_start;

    int ret = ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &(mem));
    if (ret < 0)
    {
        perror("ERROR KVM_SET_USER_MEMORY_REGION");
        return ret;
    }

    return ret;
}

void load_binary(struct vm *kvm)
{
    int fd = open("test.bin", O_RDONLY);
    int ret = 0;
    char *p = (char *)kvm->mem_start;

    while (1)
    {
        ret = read(fd, p, 4096);
        if (ret <= 0)
        {
            break;
        }
        p += ret;
    }
}

void kvm_run_vm(struct vm *kvm)
{
    pthread_create(&(kvm->vcpu->vcpu_thread), NULL, kvm->vcpu->vcpu_thread_func, kvm);
    pthread_join(kvm->vcpu->vcpu_thread, NULL);
}

void kvm_reset_vcpu(struct vcpu *vcpu)
{
    if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &(vcpu->sregs)) < 0)
    {
        perror("ERROR: KVM_GET_SREGS");
        return;
    }
    
    // The base and selector fields are both zeroed, indicating what memory address the segment points to.
    vcpu->sregs.cs.selector = 0;
    vcpu->sregs.cs.base = 0;

    if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0)
    {
        perror("ERROR: KVM_GET_SREGS");
        return;
    }

    vcpu->regs.rflags = 2;
    vcpu->regs.rip = 0;
    if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &(vcpu->regs)) < 0)
    {
        perror("EROR: KVM_SET_REGS\n");
        return;
    }
}

void * kvm_cpu_thread(void *data)
{
    uint16_t ret = 0;
    bool kvm_exit = false;
    struct vm *kvm = (struct vm *)data;


    // The initial states of these sets of registers must be set up.
    kvm_reset_vcpu(kvm->vcpu);

    while (!kvm_exit)
    {

        ret = ioctl(kvm->vcpu->vcpu_fd, KVM_RUN, NULL);

        if (ret < 0)
        {
            fprintf(stderr, "KVM_RUN failed\n");
            exit(1);
        }

        switch (kvm->vcpu->kvm_run->exit_reason)
        {
        /*
        If exit_reason is KVM_EXIT_IO, then the vcpu has
        executed a port I/O instruction which could not be satisfied by kvm.
        data_offset describes where the data is located (KVM_EXIT_IO_OUT) or
        where kvm expects application code to place the data for the next
        KVM_RUN invocation (KVM_EXIT_IO_IN).  Data format is a packed array.
        */
        case KVM_EXIT_IO:
        {
            printf("Data: %d\n", *(int *)((char *)(kvm->vcpu->kvm_run) + kvm->vcpu->kvm_run->io.data_offset));
            sleep(1);
            break;
        }
        case KVM_EXIT_HLT:
        {
            kvm_exit = true;
            break;
        }
        default:
        {
            printf("KVM ERROR\n %d", kvm->vcpu->kvm_run->exit_reason);
            kvm_exit = false;
        }
        }
    }
    return NULL;
}

struct vcpu *kvm_init_vcpu(struct vm *kvm, void *(*fn)(void *))
{
    struct vcpu *vcpu;
    
    // adds a vcpu to a virtual machine
    vcpu->vcpu_fd = ioctl(kvm->vm_fd, KVM_CREATE_VCPU, 0);

    if (vcpu->vcpu_fd < 0)
    {
        perror("ERROR: KVM_CREATE_VCPU");
        return NULL;
    }

    // size of vcpu mmap area, in bytes
    vcpu->kvm_run_mmap_size = ioctl(kvm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);

    if (vcpu->kvm_run_mmap_size < 0)
    {
        perror("ERROR: KVM_GET_VCPU_MMAP_SIZE");
        return NULL;
    }

    vcpu->kvm_run = (kvm_run *)mmap(NULL, vcpu->kvm_run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->vcpu_fd, 0);

    if (vcpu->kvm_run == MAP_FAILED)
    {
        perror("ERROR: MAP_FAILED");
        return NULL;
    }

    vcpu->vcpu_thread_func = fn;
    return vcpu;
}


int main()
{
    struct vm *kvm = kvm_init();

    kvm_create_vm(kvm);
    load_binary(kvm);

    kvm->vcpu = kvm_init_vcpu(kvm, kvm_cpu_thread);

    kvm_run_vm(kvm);

}
