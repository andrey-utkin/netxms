/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2024 Victor Kirhenshtein
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

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertEquals;
import java.io.IOException;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.netxms.client.NXCException;
import org.netxms.client.NXCSession;
import org.netxms.client.maps.NetworkMapLink;
import org.netxms.client.maps.NetworkMapPage;
import org.netxms.client.maps.elements.NetworkMapElement;
import org.netxms.client.maps.elements.NetworkMapObject;
import org.netxms.client.objects.AbstractNode;
import org.netxms.client.objects.AbstractObject;
import org.netxms.client.objects.Node;
import org.netxms.client.objects.Subnet;
import org.netxms.client.topology.FdbEntry;
import org.netxms.client.topology.Route;
import org.netxms.utilities.TestHelper;


/**
 * Tests for network topology functions
 */
public class SpecificTopologyTest extends AbstractSessionTest
{
   @Test
   public void testIPTopology() throws Exception
   {
      final NXCSession session = connectAndLogin();
      Node node = TestHelper.getTopologyNode(session);
      assertNotNull(node);

      // TODO how to add neighbour nodes explicitly?
      NetworkMapPage page = session.queryIPTopology(node.getObjectId(), -1);
      session.syncMissingObjects(page.getAllLinkStatusObjects(), 0, NXCSession.OBJECT_SYNC_WAIT); // needed?
      for(NetworkMapElement e : page.getElements())
      {
         System.out.println(e.toString());
         assertEquals(e.getType(), NetworkMapElement.MAP_ELEMENT_OBJECT);
         assert e instanceof NetworkMapObject;
         NetworkMapObject nmo = (NetworkMapObject) e;
         AbstractObject elementObject = session.findObjectById(nmo.getObjectId());
         assertNotNull(elementObject);
         System.out.println(elementObject.getObjectClassName() + ": " + elementObject.getObjectName());
      }
      for(NetworkMapLink l : page.getLinks())
         System.out.println(l.toString());

      session.disconnect();
   }
}
