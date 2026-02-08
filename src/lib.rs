#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("cli; hlt") }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_entry() {}