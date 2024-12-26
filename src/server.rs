use std::fs;
use std::path::Path;
use tokio::io::AsyncWriteExt;
use tokio::net::TcpListener;

pub fn start_server() -> std::io::Result<()> {
    tokio::runtime::Runtime::new()?.block_on(async {
        let listener = TcpListener::bind("127.0.0.1:8080").await?;
        println!("Server is listening on 127.0.0.1:8080");

        loop {
            let (mut socket, _) = listener.accept().await?;
            println!("Client connected!");

            let file_path = Path::new("example.txt");
            if file_path.exists() {
                println!("Sending file: example.txt");

                let file_content = fs::read(file_path)?;
                let file_size = file_content.len() as u32;

                socket.write_u32(file_size).await?;
                socket.write_all(&file_content).await?;
                println!("File sent successfully!");
            } else {
                eprintln!("File not found: example.txt");
                socket.write_u32(0).await?;
            }
        }
    })
}
