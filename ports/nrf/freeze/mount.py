
import nrf

class FlashBdev:
    BLOCK_SIZE = nrf.flash_block_size
    RESERVER_BLOCKS = 1
    BLOCK_START = nrf.flash_user_start() // BLOCK_SIZE + RESERVER_BLOCKS
    BLOCK_END   = nrf.flash_user_end()   // BLOCK_SIZE
    NUM_BLOCKS = BLOCK_END - BLOCK_START

    def readblocks(self, n, buf):
        nrf.flash_read((self.BLOCK_START + n) * self.BLOCK_SIZE, buf)

    def writeblocks(self, n, buf):
        nrf.flash_erase((self.BLOCK_START + n) * self.BLOCK_SIZE)
        nrf.flash_write((self.BLOCK_START + n) * self.BLOCK_SIZE, buf)

    def ioctl(self, op, arg):
        if op == 4: # BP_IOCTL_SEC_COUNT
            return self.NUM_BLOCKS
        if op == 5: # BP_IOCTL_SEC_SIZE
            return self.BLOCK_SIZE

bdev = FlashBdev()

if bdev.NUM_BLOCKS > 10: # random number
    import uos
    uos.mount(uos.VfsFat(bdev), '/')
