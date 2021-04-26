learning KVM API
----------------
By default vCPU works under Real Mode which can we execute only 16-bit assembled code. We can not execute 32/64 x86 assembled codes. 

Basic operations:
1. Open kvm 
2. Create vm
3. Set memory for vm
4. Create vcpu
5. Set memory for vcpu
6. Create threads
7. Kvm_run

~ Resources
- https://github.com/soulxu/kvmsample (I will try to use as a base example, very nice for learning KVM API)
- https://github.com/dpw/kvm-hello-world
- https://lwn.net/Articles/658511/
- https://www.kernel.org/doc/Documentation/virtual/kvm/api.txt
