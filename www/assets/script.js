function changeTitleDown() {
    var dollarSign = document.getElementById('dollarSign');
    var command = document.getElementById('command')
    dollarSign.style.display = 'none';
    command.innerHTML = 'smh108u';
}

function changeTitleUp() {
    var dollarSign = document.getElementById('dollarSign');
    var command = document.getElementById('command')
    dollarSign.style.display = 'inline';
    command.innerHTML = 'whoami';
}

function changeAboutClassToShow() {
    var about = document.getElementById('about');
    var contact = document.getElementById('contact');
    contact.style.display = 'none';
    about.style.display = 'block';
    about.className = 'aboutShow';
}

function changeContactClassToShow() {
    var contact = document.getElementById('contact');
    var about = document.getElementById('about');
    about.style.display = 'none';
    contact.style.display = 'block';
    contact.className = 'contactShow';
}

function showAbout() {
    document.getElementById('previousButton').style.visibility = 'hidden';
    document.getElementById('nextButton').style.visibility = 'visible';
    var contact = document.querySelector('.contactShow');
    if (contact) {
        contact.className = "contactHidden";
        contact.addEventListener("transitionend", changeAboutClassToShow, {once: true});
    }
    else {
        changeAboutClassToShow();
    }
}

function showContact() {
    document.getElementById('nextButton').style.visibility = 'hidden';
    document.getElementById('previousButton').style.visibility = 'visible';
    var about = document.querySelector('.aboutShow');
    if (about) {
        about.className = "aboutHidden";
        about.addEventListener("transitionend", changeContactClassToShow, {once: true});
    }
    else {
        changeContactClassToShow();
    }
}
