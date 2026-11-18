package dev.esplink.controller;

interface LinkListener {
    void onState(String message, boolean ready, boolean connecting);
    void onReply(String reply);
    void onError(String message);
}
