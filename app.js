// Import the functions you need from the SDKs you need
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-app.js";
import { getAnalytics } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-analytics.js";
import { getAuth, createUserWithEmailAndPassword, signInWithEmailAndPassword } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-auth.js";

// Your web app's Firebase configuration
const firebaseConfig = {
    apiKey: "AIzaSyC8ORguVAR-tG6QFawBrpUWxvy_FjcnDN0",
    authDomain: "douchess2024.firebaseapp.com",
    projectId: "douchess2024",
    storageBucket: "douchess2024.appspot.com",
    messagingSenderId: "849463658201",
    appId: "1:849463658201:web:e2b9f22e0c25d56f49e0c8",
    measurementId: "G-KSZ64T6XW6"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);
const auth = getAuth();

// Sign Up
document.getElementById('signup-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const email = document.getElementById('signup-email').value;
    const password = document.getElementById('signup-password').value;

    createUserWithEmailAndPassword(auth, email, password)
        .then((userCredential) => {
            // Signed in
            const user = userCredential.user;
            alert('User signed up successfully!');
        })
        .catch((error) => {
            const errorCode = error.code;
            const errorMessage = error.message;
            alert(`Error: ${errorMessage}`);
        });
});

// Sign In
document.getElementById('signin-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const email = document.getElementById('signin-email').value;
    const password = document.getElementById('signin-password').value;

    signInWithEmailAndPassword(auth, email, password)
        .then((userCredential) => {
            // Signed in
            const user = userCredential.user;
            alert('User signed in successfully!');
        })
        .catch((error) => {
            const errorCode = error.code;
            const errorMessage = error.message;
            alert(`Error: ${errorMessage}`);
        });
});

// Chessboard setup
document.addEventListener("DOMContentLoaded", function() {
    const board = Chessboard('chessboard', {
        draggable: true,
        dropOffBoard: 'trash',
        sparePieces: true
    });

    // Chess game logic (example)
    const game = new Chess();

    // Handle piece drop
    const onDrop = (source, target) => {
        const move = game.move({
            from: source,
            to: target,
            promotion: 'q' // always promote to a queen for simplicity
        });

        // Illegal move
        if (move === null) return 'snapback';
    };

    // Set up the position after the piece snap
    const onSnapEnd = () => {
        board.position(game.fen());
    };

    // Configure the chessboard
    board = Chessboard('chessboard', {
        draggable: true,
        position: 'start',
        onDrop: onDrop,
        onSnapEnd: onSnapEnd
    });
});
