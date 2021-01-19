/**
 * 
 */
package org.netxms.reporting.tools;

import java.io.File;
import java.util.Properties;
import javax.activation.DataHandler;
import javax.activation.FileDataSource;
import javax.mail.BodyPart;
import javax.mail.Message;
import javax.mail.Multipart;
import javax.mail.Session;
import javax.mail.Transport;
import javax.mail.internet.InternetAddress;
import javax.mail.internet.MimeBodyPart;
import javax.mail.internet.MimeMessage;
import javax.mail.internet.MimeMultipart;
import org.netxms.reporting.Server;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * SMTP sender
 */
public final class SmtpSender
{
   private static Logger logger = LoggerFactory.getLogger(SmtpSender.class);

   private Server server;
   private Properties properties;

   /**
    * Create new SMTP sender
    * 
    * @param server owning server
    */
   public SmtpSender(Server server)
   {
      this.server = server;

      properties = new Properties();
      properties.putAll(System.getProperties());
      properties.setProperty("mail.host", server.getConfigurationProperty("smtp.server", "localhost"));
   }

   /**
    * Send email.
    * 
    * @param recipient recipient address
    * @param subject mail subject
    * @param body mail body
    * @param fileName name of attached file or null
    * @param file file to attach or null
    */
   public void sendMail(String recipient, String subject, String body, String fileName, File file)
   {
      logger.info("Sending notification email to " + recipient);
      try
      {
         Session session = Session.getDefaultInstance(properties);
         MimeMessage message = new MimeMessage(session);
         message.setFrom(new InternetAddress(server.getConfigurationProperty("smtp.fromAddr", "reporting@localhost")));
         message.addRecipient(Message.RecipientType.TO, new InternetAddress(recipient));
         message.setSubject(subject);
         if ((fileName != null) && (file != null))
         {
            Multipart multipart = new MimeMultipart();
            BodyPart part = new MimeBodyPart();
            part.setText(body);
            multipart.addBodyPart(part);

            part = new MimeBodyPart();
            part.setDataHandler(new DataHandler(new FileDataSource(file)));
            part.setFileName(fileName);
            multipart.addBodyPart(part);

            message.setContent(multipart);
         }
         else
         {
            message.setText(body);
         }
         Transport.send(message);
      }
      catch(Exception e)
      {
         logger.error("Unable to send notification email", e);
      }
   }
}