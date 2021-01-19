/**
 * 
 */
package org.netxms.reporting;

/**
 * Database type
 */
public enum DatabaseType
{
   POSTGRESQL("org.postgresql.Driver", "org.hibernate.dialect.PostgreSQL9Dialect"),
   ORACLE("oracle.jdbc.driver.OracleDriver", "org.hibernate.dialect.Oracle10gDialect"),
   MSSQL("com.microsoft.sqlserver.jdbc.SQLServerDriver", "org.hibernate.dialect.SQLServerDialect"),
   MYSQL("com.mysql.jdbc.Driver", "org.hibernate.dialect.MySQLDialect"),
   INFORMIX("com.informix.jdbc.IfxDriver", "org.hibernate.dialect.InformixDialect");

   private final String driver;
   private final String dialect;

   DatabaseType(String driver, String dialect)
   {
      this.driver = driver;
      this.dialect = dialect;
   }

   public String getDriver()
   {
      return driver;
   }

   public String getDialect()
   {
      return dialect;
   }

   public static DatabaseType lookup(String key)
   {
      if (key == null)
         return null;

      String lowerKey = key.toLowerCase();
      if (lowerKey.equals("postgresql") || lowerKey.contains("pgsql"))
      {
         return POSTGRESQL;
      }
      else if (lowerKey.contains("oracle"))
      {
         return ORACLE;
      }
      else if (lowerKey.contains("mssql"))
      {
         return MSSQL;
      }
      else if (lowerKey.contains("mysql"))
      {
         return MYSQL;
      }
      else if (lowerKey.contains("informix"))
      {
         return INFORMIX;
      }
      else if (lowerKey.contains("sqlite"))
      {
         throw new RuntimeException("SQLite not supported");
      }
      return null;
   }
}