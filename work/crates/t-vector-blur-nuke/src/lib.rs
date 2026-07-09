unsafe extern "C" {
    fn t_vector_blur_keepalive();
}

#[unsafe(no_mangle)]
pub extern "C" fn t_vector_blur_rust_link() {
    unsafe {
        t_vector_blur_keepalive();
    }
}
