// Import the functions you need from the SDKs you need
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-app.js";
import { getAnalytics } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-analytics.js";
// TODO: Add SDKs for Firebase products that you want to use
// https://firebase.google.com/docs/web/setup#available-libraries

// Your web app's Firebase configuration
// For Firebase JS SDK v7.20.0 and later, measurementId is optional
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
