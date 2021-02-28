# Collective and RPC Transport (CaRT)

> :warning:**警告：** CaRT正在大力开发中。使用风险自负。

CaRT是为大数据和Exascale HPC开发的开源**RPC传输层**。
它支持传统的**P2P RPC传递**和**集体RPC**，后者使用可伸缩的基于树的消息传播在一组目标服务器上调用RPC。

# Gurt Useful Routines and Types (GURT) 简单、有用的例程和类型

GURT是一个**帮助函数**和**数据类型**的开源库。该库使列表list、哈希表，堆和日志记录的操作变得容易。

所有Gurt有用的例程和类型均以“d”作为前缀，字母“d”是字母表中的第四个字母，因为Gurt使用的单词具有4个字母。

## License

CaRT是在BSD许可下分发的开源软件。请参阅[LICENSE]（./ LICENSE）和[NOTICE]（./ NOTICE）文件以获取更多信息。

## Build

CaRT需要具有C99功能的编译器和scons构建工具来构建。

CaRT依赖于某些第三方库：
- 用于RPC和底层通讯的Mercury（https://github.com/mercury-hpc/mercury），mercury使用openpa（http://git.mcs.anl.gov/radix/openpa.git）进行原子操作。
- PMIx (https://github.com/pmix/master)
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").

安装所有相关模块后，可以在顶级源目录中执行“ scons”以进行构建。
