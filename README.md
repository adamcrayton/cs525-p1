# Project 1: Simple Mail Client

- Name: Adam Crayton
- Email: adamcrayton@u.boisestate.edu
- Class: CS525-001

## Known Bugs or Issues

In the project, all the code works correctly and functions like a simple mail client. All code connects to each other correctly, provides 100% coverage, and has no memory leaks.

## Design
The simple mail client is split into three different layers. A pure protocol layer that only transforms strings into string copies and status code without any Input and Output of any kind. A session layer that runs the full conversation that the SMTP client uses. However, it only talks to the outside world through a pair of read/write pointers. And finally, a socket layer that wires all of those callbacks to real calls (getaddrinfo, connect, recv, and send). The reason this split is implemented is due to the unit testing of this project. To fully make sure a created SMTP client is working correctly, you need to test it against a live mail server and in order to do that, you need to push every socket call down into the bottom layer which in turn lets the session layer be driven by a scripted, in-memory transport instead, with no network involved. This allows us to simulate a live SMTP client without having to use a network to connect to it. 

## Experience

Overall, this project was very fun to work on. At the beginning, it was a little hard to truly understand what was fully going on. In that, I had Claude AI help explain more about what an SMTP server might look like, coding wise. Not only that, but I also had it explain what everything was doing, how they were connected, and what the expected outcome should be. Through that, this gave me a much better understanding on what I was meant to do which allowed me to make tweaks and do the bulk of the code. This project was a very good introduction to what we will be doing in this class, and I am excited to see what is to come. If I were to improve on anything, I think it would be researching more about the topic before diving head first into the project. As much as AI is suggested, I still would like to have a better understanding as a whole before I have to ask it to assist. Overall though, very happy with the outcome of this project. Can't wait to see what's next!
