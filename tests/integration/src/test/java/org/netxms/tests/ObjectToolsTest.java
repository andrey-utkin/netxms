/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.tests;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import java.io.StringWriter;
import java.util.List;
import java.util.Map;
import java.util.HashMap;
import org.junit.jupiter.api.Test;
import org.netxms.client.InputField;
import org.netxms.client.NXCSession;
import org.netxms.client.constants.InputFieldType;
import org.netxms.client.objecttools.ObjectTool;
import org.netxms.client.objecttools.ObjectToolDetails;
import org.netxms.client.objects.Node;
import org.netxms.utilities.TestHelper;


import org.netxms.utilities.TestHelper;
import org.netxms.client.TextOutputListener;

class TextOutputChecker implements TextOutputListener
{
   /**
    * @see org.netxms.client.TextOutputListener#messageReceived(java.lang.String)
    */
   @Override
   public void messageReceived(String text)
   {
      System.out.println("messageReceived: " + text);
   }

   /**
    * @see org.netxms.client.TextOutputListener#setStreamId(long)
    */
   @Override
   public void setStreamId(long streamId)
   {
   }

   /**
    * @see org.netxms.client.TextOutputListener#onSuccess()
    */
   @Override
   public void onSuccess()
   {
      System.out.println("Success");
   }

   /**
    * @see org.netxms.client.TextOutputListener#onFailure(java.lang.Exception)
    */
   @Override
   public void onFailure(Exception exception)
   {
      assertFalse(true);
   }
}


/**
 * 
 * Test object tools functionality
 *
 */
public class ObjectToolsTest extends AbstractSessionTest
{
   @Test
   public void testGet() throws Exception
   {
      final NXCSession session = connect();

      List<ObjectTool> tools = session.getObjectTools();
      for(ObjectTool tool : tools)
      {
         System.out.println(" >>Tool>> " + tool.getId() + " " + tool.getName());
      }

      session.disconnect();
   }

   @Test
   public void testGetDetails() throws Exception
   {
      final NXCSession session = connect();
      List <ObjectTool> allObjectTools = session.getObjectTools();
      ObjectTool testObjectTool = null;
      if (!allObjectTools.isEmpty()) {
         testObjectTool = allObjectTools.get(0);
      }
      ObjectToolDetails td = session.getObjectToolDetails(testObjectTool.getId());
      System.out.println("Object tool details:");
      System.out.println("   id = " + td.getId());
      System.out.println("   name = " + td.getName());
      System.out.println("   type = " + td.getToolType());
      System.out.println("   OID = " + td.getSnmpOid());
      System.out.println("   confirmation = " + td.getConfirmationText());
      System.out.println("   columnCount = " + td.getColumns().size());

      session.disconnect();
   }

   @Test
   public void testGenerateId() throws Exception
   {
      final NXCSession session = connect();

      long id = session.generateObjectToolId();
      assertFalse(id == 0);

      System.out.println("Object tool ID generated: " + id);

      session.disconnect();
   }

   @Test
   public void testCreateAndDelete() throws Exception
   {
      final NXCSession session = connect();

      long id = session.generateObjectToolId();
      assertFalse(id == 0);
      System.out.println("Object tool ID generated: " + id);

      ObjectToolDetails td = new ObjectToolDetails(id, ObjectTool.TYPE_LOCAL_COMMAND, "UnitTest");
      td.setData("ping %{10%(not a field)} -t %(address) -s %(size)");
      td.addInputField(new InputField("size", InputFieldType.NUMBER, "Size (bytes)", 0));
      td.addInputField(new InputField("unused", InputFieldType.PASSWORD, "Unused field", 0));
      session.modifyObjectTool(td);

      td = session.getObjectToolDetails(id);
      for(InputField f : td.getInputFields())
         System.out.println(f);

      session.deleteObjectTool(id);

      session.disconnect();
   }

   @Test
   public void testInputGlobalVar() throws Exception
   {
      final NXCSession session = connect();

      long id = session.generateObjectToolId();
      assertFalse(id == 0);
      System.out.println("Object tool ID generated: " + id);

      ObjectToolDetails td = new ObjectToolDetails(id, ObjectTool.TYPE_SERVER_SCRIPT, "testInputGlobalVar");
      td.setData("println($INPUT);");
      td.addInputField(new InputField("size", InputFieldType.NUMBER, "Size (bytes)", 0));
      td.addInputField(new InputField("unused", InputFieldType.PASSWORD, "Unused field", 0));
      session.modifyObjectTool(td);

      td = session.getObjectToolDetails(id);
      for(InputField f : td.getInputFields())
         System.out.println(f);


      Node testNode = (Node)TestHelper.findManagementServer(session);
      assertNotNull(testNode);

      Map<String, String> inputFields = new HashMap<String, String>();
      inputFields.put("size", "1024");
      inputFields.put("unused", "");

      StringWriter stringWriter = new StringWriter();
      //session.executeServerCommand(/*objectId*/testNode.getObjectId(), /*alarmId*/0, /*command*/td.getData(), inputFields, /*maskedFields*/null, /*receiveOutput*/true, /*listener*/null, stringWriter);
      TextOutputChecker listener = new TextOutputChecker();
      session.executeLibraryScript(/*objectId*/testNode.getObjectId(), /*alarmId*/0, /*command*/td.getData(), inputFields, /*maskedFields*/null, /*receiveOutput*/true, /*listener*/listener);
      System.out.println("Object Tool output: " + stringWriter.toString());

      session.deleteObjectTool(id);

      session.disconnect();
   }
}
