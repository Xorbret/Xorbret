# email testing
import smtplib

gmail_user = 
gmail_password = 

sent_from = gmail_user
to = []
subject = 'THIS IS A TEST OF THE EMERGENCY EMAIL SERVICES'
body = 'Did you ever hear the tragedy of Darth Plagueis The Wise? I thought not. It’s not a story the Jedi would tell you. It’s a Sith legend. Darth Plagueis was a Dark Lord of the Sith, so powerful and so wise he could use the Force to influence the midichlorians to create life… He had such a knowledge of the dark side that he could even keep the ones he cared about from dying. The dark side of the Force is a pathway to many abilities some consider to be unnatural. He became so powerful… the only thing he was afraid of was losing his power, which eventually, of course, he did. Unfortunately, he taught his apprentice everything he knew, then his apprentice killed him in his sleep. Ironic. He could save others from death, but not himself.'

email_text = """\
From: %s
To: %s
Subject: %s

%s
""" % (sent_from,",".join(to),subject,body)

try:
    smtp_server = smtplib.SMTP_SSL('smtp.gmail.com',465)
    smtp_server.ehlo()
    smtp_server.login(gmail_user,gmail_password)
    smtp_server.sendmail(sent_from,to,email_text)
    smtp_server.close()
    print("Email sent successfully!")
except Exception as ex:
    print("Something went wrong...",ex)
