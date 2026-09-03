#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// Flattened Device Tree constants.
#define FDT_MAGIC      0xd00dfeed
#define FDT_BEGIN_NODE 1
#define FDT_END_NODE   2
#define FDT_PROP       3
#define FDT_NOP        4
#define FDT_END        9

struct fdt_header {
  uint32 magic;
  uint32 totalsize;
  uint32 off_dt_struct;
  uint32 off_dt_strings;
  uint32 off_mem_rsvmap;
  uint32 version;
  uint32 last_comp_version;
  uint32 boot_cpuid_phys;
  uint32 size_dt_strings;
  uint32 size_dt_struct;
};

// Physical memory discovered from the device tree.
uint64 ram_base;
uint64 ram_size;
uint64 phystop;

// Read a big-endian 32-bit integer.
static uint32
fdt32(const void *p)
{
  const uint8 *b = (const uint8 *)p;

  return ((uint32)b[0] << 24) | ((uint32)b[1] << 16) | ((uint32)b[2] << 8) |
         ((uint32)b[3]);
}

// Read a value consisting of n 32-bit DT cells.
// QEMU virt uses at most two cells here.
static uint64
read_cells(const uint8 *p, uint32 n)
{
  uint64 x = 0;

  if (n > 2)
    panic("fdt: too many cells");

  for (uint32 i = 0; i < n; i++) {
    x <<= 32;
    x |= fdt32(p + i * 4);
  }

  return x;
}

// FDT structure entries are padded to a 4-byte boundary.
static uint8 *
align4(uint8 *p)
{
  return (uint8 *)(((uint64)p + 3) & ~3UL);
}

static int
streq(const char *a, const char *b)
{
  int na = strlen(a);
  int nb = strlen(b);

  if (na != nb)
    return 0;

  return strncmp(a, b, na) == 0;
}

// According to the device-tree convention, a memory node is named
// "memory" or "memory@<unit-address>".
static int
is_memory_node(const char *name)
{
  if (strncmp(name, "memory", 6) != 0)
    return 0;

  return name[6] == '\0' || name[6] == '@';
}

void
fdtinit(void *dtb)
{
  struct fdt_header *h = (struct fdt_header *)dtb;

  if (dtb == 0)
    panic("fdt: null");

  if (fdt32(&h->magic) != FDT_MAGIC)
    panic("fdt: bad magic");

  uint32 totalsize = fdt32(&h->totalsize);
  uint32 off_dt_struct = fdt32(&h->off_dt_struct);
  uint32 off_dt_strings = fdt32(&h->off_dt_strings);
  uint32 size_dt_struct = fdt32(&h->size_dt_struct);
  uint32 size_dt_strings = fdt32(&h->size_dt_strings);

  // Basic bounds checks.
  if (off_dt_struct >= totalsize || off_dt_strings >= totalsize ||
      size_dt_struct > totalsize - off_dt_struct ||
      size_dt_strings > totalsize - off_dt_strings)
    panic("fdt: bad offsets");

  uint8 *base = (uint8 *)dtb;

  uint8 *p = base + off_dt_struct;
  uint8 *struct_end = p + size_dt_struct;

  char *strings = (char *)(base + off_dt_strings);

  // DT specification defaults if the properties are absent.
  uint32 address_cells = 2;
  uint32 size_cells = 1;

  // Root node will have depth 0.
  int depth = -1;

  // -1 means we are currently outside the memory node.
  int memory_depth = -1;

  int found_memory = 0;

  while (p + 4 <= struct_end) {
    uint32 token = fdt32(p);
    p += 4;

    switch (token) {

    case FDT_BEGIN_NODE: {
      char *name = (char *)p;

      // Skip the NUL-terminated node name.
      while (p < struct_end && *p != 0)
        p++;

      if (p >= struct_end)
        panic("fdt: bad node name");

      p++;           // skip '\0'
      p = align4(p); // next token is 4-byte aligned

      depth++;

      if (depth == 1 && is_memory_node(name))
        memory_depth = depth;

      break;
    }

    case FDT_END_NODE:
      if (depth == memory_depth)
        memory_depth = -1;

      depth--;
      break;

    case FDT_PROP: {
      if (p + 8 > struct_end)
        panic("fdt: bad property");

      uint32 len = fdt32(p);
      uint32 nameoff = fdt32(p + 4);
      p += 8;

      if (nameoff >= size_dt_strings)
        panic("fdt: bad property name");

      if (len > (uint64)(struct_end - p))
        panic("fdt: bad property length");

      char *name = strings + nameoff;
      uint8 *value = p;

      //
      // Properties of the root node tell us how to decode
      // addresses and sizes in child nodes.
      //
      if (depth == 0 && streq(name, "#address-cells")) {
        if (len != 4)
          panic("fdt: address-cells");

        address_cells = fdt32(value);
      }

      if (depth == 0 && streq(name, "#size-cells")) {
        if (len != 4)
          panic("fdt: size-cells");

        size_cells = fdt32(value);
      }

      //
      // The memory node's "reg" property contains:
      //
      //   <address ... size ...>
      //
      if (depth == memory_depth && memory_depth != -1 && streq(name, "reg") &&
          !found_memory) {

        uint32 address_bytes = address_cells * 4;
        uint32 size_bytes = size_cells * 4;

        if (len < address_bytes + size_bytes)
          panic("fdt: short memory reg");

        ram_base = read_cells(value, address_cells);

        ram_size = read_cells(value + address_bytes, size_cells);

        if (ram_size == 0)
          panic("fdt: zero memory");

        if (ram_base + ram_size < ram_base)
          panic("fdt: memory overflow");

        //
        // xv6 is linked to run with RAM beginning at KERNBASE.
        // QEMU virt satisfies this.
        //
        if (ram_base != KERNBASE)
          panic("fdt: unexpected RAM base");

        //
        // Only whole physical pages can be managed.
        //
        phystop = (ram_base + ram_size) & ~(PGSIZE - 1);

        found_memory = 1;
      }

      p += len;
      p = align4(p);

      break;
    }

    case FDT_NOP:
      break;

    case FDT_END:
      if (!found_memory)
        panic("fdt: no memory node");

      return;

    default:
      panic("fdt: unknown token");
    }
  }

  panic("fdt: truncated");
}
