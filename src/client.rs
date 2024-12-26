use tokio::net::TcpStream;
use tokio::io::AsyncReadExt;
use std::fs::File;
use std::io::Write;

pub fn start_client() -> std::io::Result<()> {
    tokio::runtime::Runtime::new()?.block_on(async {
        let mut socket = TcpStream::connect("127.0.0.1:8080").await?;
        println!("Connected to server!");

        let file_size = socket.read_u32().await?;
        if file_size == 0 {
            eprintln!("Server reported: File not found.");
            return Ok(());
        }

        println!("Receiving file of size: {} bytes", file_size);

        let mut buffer = vec![0; file_size as usize];
        socket.read_exact(&mut buffer).await?;

        let mut file = File::create("received_example.txt")?;
        file.write_all(&buffer)?;
        println!("File received and saved as 'received_example.txt'.");

        Ok(())
    })
}
