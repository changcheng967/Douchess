// Import the functions you need from the SDKs you need
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-app.js";
import { getAnalytics } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-analytics.js";
import { getAuth, createUserWithEmailAndPassword, signInWithEmailAndPassword, signOut, onAuthStateChanged, updateProfile } from "https://www.gstatic.com/firebasejs/10.12.4/firebase-auth.js";

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

// Chess game setup
const game = new Chess();
let board;

// Sign Up
document.getElementById('signup-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const email = document.getElementById('signup-email').value;
    const password = document.getElementById('signup-password').value;

    createUserWithEmailAndPassword(auth, email, password)
        .then((userCredential) => {
            // Signed up
            const user = userCredential.user;
            alert('User signed up successfully!');
            document.getElementById('signup-form').reset();
        })
        .catch((error) => {
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
            document.getElementById('signin-form').reset();
            loadProfile(user);
        })
        .catch((error) => {
            const errorMessage = error.message;
            alert(`Error: ${errorMessage}`);
        });
});

// Sign Out
document.getElementById('signout-button').addEventListener('click', () => {
    signOut(auth).then(() => {
        alert('User signed out successfully!');
        hideProfile();
    }).catch((error) => {
        const errorMessage = error.message;
        alert(`Error: ${errorMessage}`);
    });
});

// Profile Management
const loadProfile = (user) => {
    document.getElementById('profile-section').style.display = 'block';
    document.getElementById('user-email').innerText = user.email;
    document.getElementById('username').value = user.displayName || '';
};

const hideProfile = () => {
    document.getElementById('profile-section').style.display = 'none';
    document.getElementById('user-email').innerText = '';
    document.getElementById('username').value = '';
};

document.getElementById('update-profile-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const user = auth.currentUser;

    updateProfile(user, {
        displayName: username
    }).then(() => {
        alert('Profile updated successfully!');
    }).catch((error) => {
        const errorMessage = error.message;
        alert(`Error: ${errorMessage}`);
    });
});

// Initialize board and game
document.addEventListener("DOMContentLoaded", function () {
    board = Chessboard('chessboard', {
        draggable: true,
        position: 'start',
        onDrop: (source, target) => {
            const move = game.move({
                from: source,
                to: target,
                promotion: 'q' // always promote to a queen for simplicity
            });

            if (move === null) return 'snapback';

            updateStatus();
        },
        onSnapEnd: () => {
            board.position(game.fen());
        }
    });
});

const updateStatus = () => {
    let status = '';
    if (game.in_checkmate()) {
        status = 'Game over, ' + (game.turn() === 'w' ? 'Black' : 'White') + ' is in checkmate.';
    } else if (game.in_draw()) {
        status = 'Game over, drawn position';
    } else {
        status = game.turn() === 'w' ? 'White to move' : 'Black to move';

        if (game.in_check()) {
            status += ', ' + (game.turn() === 'w' ? 'White' : 'Black') + ' is in check';
        }
    }
    document.getElementById('status').innerText = status;
};

// Monitor auth state
onAuthStateChanged(auth, (user) => {
    if (user) {
        loadProfile(user);
    } else {
        hideProfile();
    }
});
