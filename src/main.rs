mod client;
mod server;

use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: cargo run -- <server|client>");
        return;
    }

    match args[1].as_str() {
        "server" => {
            println!("Starting server...");
            if let Err(e) = server::start_server() {
                eprintln!("Server error: {}", e);
            }
        }
        "client" => {
            println!("Starting client...");
            if let Err(e) = client::start_client() {
                eprintln!("Client error: {}", e);
            }
        }
        _ => {
            eprintln!("Invalid argument. Use 'server' or 'client'.");
        }
    }
}
